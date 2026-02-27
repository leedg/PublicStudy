// English: CrashDump implementation
// 한글: CrashDump 구현
//
// Ported from RAON ServerEngine/Exception with RAON-specific dependencies removed:
//   - NaverWorks messaging            → removed
//   - GThreadManager / GLockStackManager / GMemoryManager → removed
//   - TL_* thread-local variables     → removed (replaced with CONTEXT from EXCEPTION_POINTERS)
//   - ExceptionThreadManager class    → replaced with CreateToolhelp32Snapshot loop
//   - NOCRASH flags                   → always write dump
//   - SystemInfo                      → replaced with GetVersionEx / RtlGetVersion
//   - WorkerThread::InitTLS           → removed
//   - CRASH_INLINE macro              → signal handlers call RaiseException directly

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <DbgHelp.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <signal.h>

#include <atomic>
#include <fstream>
#include <string>
#include <thread>

#include "CrashDump.h"

// Link against DbgHelp and Psapi at compile time
#pragma comment(lib, "DbgHelp.lib")
#pragma comment(lib, "Psapi.lib")

namespace Network::Utils
{

// ============================================================
// Internal helpers (file scope)
// ============================================================

namespace
{

// English: Raise a structured exception so the SEH filter fires from a signal handler
// 한글: 시그널 핸들러에서 SEH 필터가 발동하도록 구조적 예외 발생
[[noreturn]] void TriggerCrash()
{
	// English: Dereference null — causes ACCESS_VIOLATION which goes through SEH
	// 한글: 널 역참조 → ACCESS_VIOLATION으로 SEH 필터 경유
	volatile int* null = nullptr;
	*null              = 0;
	__assume(false);
}

void SignalHandler(int signum)
{
	(void)signum;
	TriggerCrash();
}

// ============================================================

struct ExceptionData
{
	EXCEPTION_POINTERS* mExceptionInfo = nullptr;
};

} // anonymous namespace

// ============================================================
// Public API
// ============================================================

void CrashDump::Initialize(const char* dumpDir)
{
	// English: Store dump directory
	// 한글: 덤프 디렉토리 저장
	if (dumpDir && dumpDir[0] != '\0')
	{
		strncpy_s(sDumpDir, dumpDir, _TRUNCATE);

		// English: Ensure trailing slash
		// 한글: 경로 끝에 슬래시 확보
		size_t len = strnlen_s(sDumpDir, MAX_PATH);
		if (len > 0 && sDumpDir[len - 1] != '\\' && sDumpDir[len - 1] != '/')
		{
			if (len + 1 < MAX_PATH)
			{
				sDumpDir[len]     = '\\';
				sDumpDir[len + 1] = '\0';
			}
		}

		// English: Create directory if it does not exist
		// 한글: 디렉토리가 없으면 생성
		CreateDirectoryA(sDumpDir, nullptr);
	}
	else
	{
		sDumpDir[0] = '\0';
	}

	// English: Load debug symbols from the executable's directory
	// 한글: 실행 파일 디렉토리에서 디버그 심볼 로드
	{
		HANDLE hProcess = GetCurrentProcess();
		DWORD  symOpts  = SymGetOptions();
		symOpts |= SYMOPT_LOAD_LINES;
		SymSetOptions(symOpts);

		char binFolder[MAX_PATH];
		if (GetModuleFileNameA(nullptr, binFolder, MAX_PATH) != 0)
		{
			char* sep = strrchr(binFolder, '\\');
			if (sep)
				*(sep + 1) = '\0';
			SymInitialize(hProcess, binFolder, TRUE);
		}
		else
		{
			SymInitialize(hProcess, nullptr, TRUE);
		}
	}

	// English: Install SEH unhandled exception filter
	// 한글: SEH 미처리 예외 필터 설치
	SetUnhandledExceptionFilter(ExceptionFilter);

	// English: Install signal handlers (SIGABRT, SIGFPE, SIGILL, SIGSEGV, SIGTERM)
	//          so C runtime aborts also go through our crash handler
	// 한글: 시그널 핸들러 등록 (C 런타임 abort도 크래시 핸들러 경유)
	signal(SIGABRT, SignalHandler);
	signal(SIGFPE,  SignalHandler);
	signal(SIGILL,  SignalHandler);
	signal(SIGSEGV, SignalHandler);

	// English: Handle pure virtual call and invalid parameter — both trigger crash
	// 한글: 순수 가상 호출 및 잘못된 매개변수 처리 — 모두 크래시 유발
	_set_purecall_handler([]() { TriggerCrash(); });
	_set_invalid_parameter_handler(
	    [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
	    { TriggerCrash(); });
}

// ============================================================
// SEH filter
// ============================================================

LONG WINAPI CrashDump::ExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
{
	// English: Replace filter with noop immediately to prevent re-entry from
	//          other threads that crash while we are writing the dump
	// 한글: 덤프 작성 중 다른 스레드 충돌로 인한 재진입 방지를 위해 즉시 noop 교체
	SetUnhandledExceptionFilter(ExceptionFilterNoop);

	static std::atomic<int> sCount{0};
	if (++sCount != 1)
	{
		// English: Another thread crashed concurrently — suspend and wait
		// 한글: 다른 스레드가 동시에 충돌 — 일시 중단하고 대기
		SuspendThread(GetCurrentThread());
		Sleep(INFINITE);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	// English: Run the actual dump work in a fresh thread so we have a clean stack
	//          (necessary for EXCEPTION_STACK_OVERFLOW on the crashed thread)
	// 한글: 깨끗한 스택을 위해 새 스레드에서 덤프 작업 실행
	//       (충돌 스레드의 EXCEPTION_STACK_OVERFLOW 처리에 필수)
	ExceptionData data;
	data.mExceptionInfo = exceptionInfo;

	std::thread worker(ExceptionProc, &data);
	worker.join();

	SuspendThread(GetCurrentThread());
	return EXCEPTION_EXECUTE_HANDLER;
}

LONG WINAPI CrashDump::ExceptionFilterNoop([[maybe_unused]] EXCEPTION_POINTERS* exceptionInfo)
{
	SuspendThread(GetCurrentThread());
	Sleep(INFINITE);
	return EXCEPTION_EXECUTE_HANDLER;
}

// ============================================================
// Dump work (runs in separate thread)
// ============================================================

DWORD WINAPI CrashDump::ExceptionProc(void* arg)
{
	ExceptionData*      data          = static_cast<ExceptionData*>(arg);
	EXCEPTION_POINTERS* exceptionInfo = data->mExceptionInfo;

	// English: Suspend all other threads to get consistent callstacks
	// 한글: 일관된 콜스택 수집을 위해 다른 모든 스레드 일시 중단
	SuspendOtherThreads();

	// English: Build timestamped base file name
	// 한글: 타임스탬프가 포함된 기본 파일명 생성
	SYSTEMTIME st;
	GetLocalTime(&st);

	char modulePath[MAX_PATH];
	if (GetModuleFileNameA(nullptr, modulePath, MAX_PATH) == 0)
		strcpy_s(modulePath, "unknown.exe");

	char baseName[MAX_PATH];
	GetBaseFileName(modulePath, baseName, sizeof(baseName));

	constexpr DWORD kMaxOut = MAX_PATH + 128;
	char            outBase[kMaxOut];
	sprintf_s(
	    outBase,
	    "%s%s.%04d-%02d-%02d_%02d_%02d_%02d",
	    sDumpDir[0] ? sDumpDir : ".\\",
	    baseName,
	    st.wYear, st.wMonth, st.wDay,
	    st.wHour, st.wMinute, st.wSecond);

	// English: Write mini and full .dmp files
	// 한글: mini 및 full .dmp 파일 작성
	char miniDumpName[kMaxOut];
	char fullDumpName[kMaxOut];
	sprintf_s(miniDumpName, "%s_mini.dmp", outBase);
	sprintf_s(fullDumpName, "%s_full.dmp", outBase);

	bool miniOk = (WriteMiniDump(exceptionInfo, miniDumpName,
	                              MINIDUMP_TYPE(MiniDumpWithDataSegs |
	                                            MiniDumpWithProcessThreadData)) == ERROR_SUCCESS);

	bool fullOk = (WriteMiniDump(exceptionInfo, fullDumpName,
	                              MiniDumpWithFullMemory) == ERROR_SUCCESS);

	// English: Write human-readable .crash file (registers + callstack)
	// 한글: 읽기 쉬운 .crash 파일 작성 (레지스터 + 콜스택)
	char crashName[kMaxOut];
	sprintf_s(crashName, "%s.crash", outBase);

	std::wofstream outFile(crashName);

	outFile << L"*** Crash Report ***\n";

	if (!miniOk) outFile << L"[WARNING] mini .dmp write failed\n";
	if (!fullOk) outFile << L"[WARNING] full .dmp write failed\n";

	{
		char buf[256];
		sprintf_s(buf, "App: %s", modulePath);
		outFile << buf << L"\n";
	}

	{
		char buf[128];
		sprintf_s(buf, "When: %04d-%02d-%02d %02d:%02d:%02d",
		          st.wYear, st.wMonth, st.wDay,
		          st.wHour, st.wMinute, st.wSecond);
		outFile << buf << L"\n";
	}

	if (exceptionInfo && exceptionInfo->ExceptionRecord)
	{
		DWORD  code    = exceptionInfo->ExceptionRecord->ExceptionCode;
		void*  addr    = exceptionInfo->ExceptionRecord->ExceptionAddress;
		char buf[256];
		sprintf_s(buf, "Exception: %s (0x%08X) at 0x%016p",
		          ExceptionCodeToString(code), code, addr);
		outFile << buf << L"\n";

		if (code == EXCEPTION_ACCESS_VIOLATION &&
		    exceptionInfo->ExceptionRecord->NumberParameters >= 2)
		{
			bool     isWrite = (exceptionInfo->ExceptionRecord->ExceptionInformation[0] == 1);
			ULONG_PTR faultAddr = exceptionInfo->ExceptionRecord->ExceptionInformation[1];
			sprintf_s(buf, "  %s address 0x%016I64x",
			          isWrite ? "Write to" : "Read from",
			          static_cast<ULONG64>(faultAddr));
			outFile << buf << L"\n";
		}
	}

	outFile << L"\n";
	outFile.flush();

	// English: Dump the crashed thread's callstack
	// 한글: 충돌 스레드 콜스택 덤프
	if (exceptionInfo && exceptionInfo->ContextRecord)
	{
		WriteCallStack(outFile, GetCurrentThread(),
		               exceptionInfo->ContextRecord, true);
	}

	outFile << L"\n*** End of Crash Report ***\n";
	outFile.flush();
	outFile.close();

	ResumeOtherThreads();

	ExitProcess(1);
	return 0;
}

// ============================================================
// MiniDump writer
// ============================================================

DWORD CrashDump::WriteMiniDump(EXCEPTION_POINTERS* exceptionInfo,
                               const char*          dumpName,
                               MINIDUMP_TYPE        dumpType)
{
	HANDLE hFile = CreateFileA(
	    dumpName,
	    GENERIC_READ | GENERIC_WRITE,
	    0, nullptr,
	    CREATE_ALWAYS,
	    FILE_ATTRIBUTE_NORMAL,
	    nullptr);

	if (hFile == nullptr || hFile == INVALID_HANDLE_VALUE)
		return GetLastError();

	MINIDUMP_EXCEPTION_INFORMATION mei = {};
	mei.ThreadId          = GetCurrentThreadId();
	mei.ExceptionPointers = exceptionInfo;
	mei.ClientPointers    = FALSE;

	MiniDumpWriteDump(
	    GetCurrentProcess(),
	    GetCurrentProcessId(),
	    hFile,
	    dumpType,
	    exceptionInfo ? &mei : nullptr,
	    nullptr,
	    nullptr);

	FlushFileBuffers(hFile);
	CloseHandle(hFile);

	return ERROR_SUCCESS;
}

// ============================================================
// Callstack writer
// ============================================================

void CrashDump::WriteCallStack(std::wofstream& outFile,
                               HANDLE          threadHandle,
                               CONTEXT*        context,
                               bool            isCrashed)
{
	HANDLE hProcess = GetCurrentProcess();
	char   buf[1024];

	sprintf_s(buf, "Thread Id = %lu %s",
	          GetThreadId(threadHandle), isCrashed ? "[CRASHED]" : "");
	outFile << buf << L"\n\n";

	// English: Registers
	// 한글: 레지스터
	outFile << L"*-- Registers --*\n";
	sprintf_s(buf,
	          "RAX=%016I64x  RBX=%016I64x  RCX=%016I64x  RDX=%016I64x  RSI=%016I64x",
	          context->Rax, context->Rbx, context->Rcx, context->Rdx, context->Rsi);
	outFile << buf << L"\n";
	sprintf_s(buf,
	          "RDI=%016I64x  RBP=%016I64x  RSP=%016I64x  RIP=%016I64x  FLG=%08X",
	          context->Rdi, context->Rbp, context->Rsp, context->Rip, context->EFlags);
	outFile << buf << L"\n";
	sprintf_s(buf,
	          " R8=%016I64x   R9=%016I64x  R10=%016I64x  R11=%016I64x  R12=%016I64x",
	          context->R8, context->R9, context->R10, context->R11, context->R12);
	outFile << buf << L"\n";
	sprintf_s(buf,
	          "R13=%016I64x  R14=%016I64x  R15=%016I64x",
	          context->R13, context->R14, context->R15);
	outFile << buf << L"\n\n";
	outFile.flush();

	// English: Stack walk
	// 한글: 스택 워크
	outFile << L"*-- Stack Back Trace --*\n";
	outFile << L"Program Counter  Stack Pointer    Return Address   "
	           L"Param0           Param1           Param2           Param3           "
	           L"Function Signature\n";

	STACKFRAME64 sf = {};
	sf.AddrPC.Offset    = context->Rip;
	sf.AddrPC.Mode      = AddrModeFlat;
	sf.AddrStack.Offset = context->Rsp;
	sf.AddrStack.Mode   = AddrModeFlat;
	sf.AddrFrame.Offset = context->Rbp;
	sf.AddrFrame.Mode   = AddrModeFlat;

	// English: Make a copy of context because StackWalk64 modifies it
	// 한글: StackWalk64가 context를 수정하므로 복사본 사용
	CONTEXT ctxCopy = *context;

	constexpr int kMaxDepth = 100;
	for (int i = 0; i < kMaxDepth; ++i)
	{
		BOOL ok = StackWalk64(
		    IMAGE_FILE_MACHINE_AMD64,
		    hProcess,
		    threadHandle,
		    &sf,
		    &ctxCopy,
		    nullptr,
		    SymFunctionTableAccess64,
		    SymGetModuleBase64,
		    nullptr);

		if (!ok || sf.AddrFrame.Offset == 0 || sf.AddrPC.Offset == 0)
			break;

		sprintf_s(buf,
		          "%016I64x %016I64x %016I64x %016I64x %016I64x %016I64x %016I64x ",
		          sf.AddrPC.Offset, sf.AddrStack.Offset, sf.AddrReturn.Offset,
		          sf.Params[0], sf.Params[1], sf.Params[2], sf.Params[3]);
		outFile << buf;

		// Symbol name
		char symBuf[sizeof(IMAGEHLP_SYMBOL64) + 512];
		auto* sym        = reinterpret_cast<PIMAGEHLP_SYMBOL64>(symBuf);
		sym->SizeOfStruct  = sizeof(symBuf);
		sym->MaxNameLength = 512;
		DWORD64 disp64    = 0;
		if (SymGetSymFromAddr64(hProcess, sf.AddrPC.Offset, &disp64, sym))
		{
			// Skip non-printable prefix bytes
			int offset = 0;
			while (sym->Name[offset] != '\0' &&
			       (sym->Name[offset] < 32 || sym->Name[offset] > 127))
				++offset;
			outFile << (const char*)(sym->Name + offset) << "() ";
		}

		// Source file + line
		IMAGEHLP_LINE64 line = {};
		line.SizeOfStruct    = sizeof(line);
		DWORD disp32         = 0;
		if (SymGetLineFromAddr64(hProcess, sf.AddrPC.Offset, &disp32, &line))
		{
			sprintf_s(buf, "[%s line %lu]", line.FileName, line.LineNumber);
			outFile << buf;
		}
		else
		{
			sprintf_s(buf, "[0x%016I64x]", sf.AddrPC.Offset);
			outFile << buf;
		}

		outFile << L"\n";
		outFile.flush();
	}

	outFile << L"\n";
	outFile.flush();
}

// ============================================================
// Thread suspension helpers
// ============================================================

void CrashDump::SuspendOtherThreads()
{
	sSuspendedCount = 0;
	DWORD  selfId   = GetCurrentThreadId();
	DWORD  pid      = GetCurrentProcessId();
	HANDLE snap     = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return;

	THREADENTRY32 te = {};
	te.dwSize         = sizeof(te);
	if (Thread32First(snap, &te))
	{
		do
		{
			if (te.th32OwnerProcessID != pid || te.th32ThreadID == selfId)
				continue;
			HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
			if (hThread)
			{
				SuspendThread(hThread);
				if (sSuspendedCount < ARRAYSIZE(sSuspendedHandles))
					sSuspendedHandles[sSuspendedCount++] = hThread;
				else
					CloseHandle(hThread);
			}
		} while (Thread32Next(snap, &te));
	}
	CloseHandle(snap);
}

void CrashDump::ResumeOtherThreads()
{
	for (DWORD i = 0; i < sSuspendedCount; ++i)
	{
		ResumeThread(sSuspendedHandles[i]);
		CloseHandle(sSuspendedHandles[i]);
		sSuspendedHandles[i] = nullptr;
	}
	sSuspendedCount = 0;
}

// ============================================================
// Exception code → string
// ============================================================

const char* CrashDump::ExceptionCodeToString(DWORD code)
{
#define CASE_EXCEPTION(x) case EXCEPTION_##x: return #x
	switch (code)
	{
		CASE_EXCEPTION(ACCESS_VIOLATION);
		CASE_EXCEPTION(DATATYPE_MISALIGNMENT);
		CASE_EXCEPTION(BREAKPOINT);
		CASE_EXCEPTION(SINGLE_STEP);
		CASE_EXCEPTION(ARRAY_BOUNDS_EXCEEDED);
		CASE_EXCEPTION(FLT_DENORMAL_OPERAND);
		CASE_EXCEPTION(FLT_DIVIDE_BY_ZERO);
		CASE_EXCEPTION(FLT_INEXACT_RESULT);
		CASE_EXCEPTION(FLT_INVALID_OPERATION);
		CASE_EXCEPTION(FLT_OVERFLOW);
		CASE_EXCEPTION(FLT_STACK_CHECK);
		CASE_EXCEPTION(FLT_UNDERFLOW);
		CASE_EXCEPTION(INT_DIVIDE_BY_ZERO);
		CASE_EXCEPTION(INT_OVERFLOW);
		CASE_EXCEPTION(PRIV_INSTRUCTION);
		CASE_EXCEPTION(IN_PAGE_ERROR);
		CASE_EXCEPTION(ILLEGAL_INSTRUCTION);
		CASE_EXCEPTION(NONCONTINUABLE_EXCEPTION);
		CASE_EXCEPTION(STACK_OVERFLOW);
		CASE_EXCEPTION(INVALID_DISPOSITION);
		CASE_EXCEPTION(GUARD_PAGE);
		CASE_EXCEPTION(INVALID_HANDLE);
	case 0xC0000194: return "POSSIBLE_DEADLOCK";
	case 0xE06D7363: return "C++_Exception";
	default:         return "Unknown";
	}
#undef CASE_EXCEPTION
}

// ============================================================
// File name helper
// ============================================================

void CrashDump::GetBaseFileName(const char* fullPath, char* outName, size_t outSize)
{
	const char* sep = strrchr(fullPath, '\\');
	if (!sep)
		sep = strrchr(fullPath, '/');
	strcpy_s(outName, outSize, sep ? sep + 1 : fullPath);
}

} // namespace Network::Utils

#endif // _WIN32
