// English: Crash dump utility — Windows SEH / signal handler that writes
//          .dmp (mini + full) and .crash (text callstack) on unhandled exception.
//          Self-contained: no external project dependencies.
// 한글: 크래시 덤프 유틸리티 — 미처리 예외 발생 시 .dmp(mini+full)과
//       .crash(텍스트 콜스택) 파일을 기록하는 Windows SEH/시그널 핸들러.
//       외부 프로젝트 의존성 없이 독립적으로 동작.
#pragma once

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <DbgHelp.h>

namespace Network::Utils
{

class CrashDump
{
public:
	// English: Call once at startup (before spawning threads).
	//          dumpDir: directory for output files, e.g. "./dumps/"
	//                   nullptr or "" → current directory.
	// 한글: 스레드 생성 전 startup 시 1회 호출.
	//       dumpDir: 출력 파일 저장 디렉토리 (nullptr/"" → 현재 디렉토리)
	static void Initialize(const char* dumpDir = nullptr);

private:
	// English: SEH unhandled-exception filter — entry point
	// 한글: SEH 미처리 예외 필터 — 진입점
	static LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS* exceptionInfo);

	// English: Noop filter — installed after first exception to prevent re-entry
	// 한글: 재진입 방지용 noop 필터
	static LONG WINAPI ExceptionFilterNoop(EXCEPTION_POINTERS* exceptionInfo);

	// English: Actual dump work runs in a separate thread
	//          (avoids stack-overflow issues on the crashed thread's stack)
	// 한글: 실제 덤프 작업은 별도 스레드에서 실행
	//       (충돌 스레드 스택의 스택 오버플로 문제 방지)
	static DWORD WINAPI ExceptionProc(void* arg);

	// English: Write mini/full .dmp file via MiniDumpWriteDump
	// 한글: MiniDumpWriteDump로 .dmp 파일 작성
	static DWORD WriteMiniDump(EXCEPTION_POINTERS* exceptionInfo,
	                           const char*          dumpName,
	                           MINIDUMP_TYPE        dumpType);

	// English: Write registers + StackWalk64 callstack to wofstream
	// 한글: 레지스터 + StackWalk64 콜스택을 wofstream에 기록
	static void WriteCallStack(std::wofstream& outFile,
	                           HANDLE          threadHandle,
	                           CONTEXT*        context,
	                           bool            isCrashed);

	// English: Suspend / resume all threads except the caller
	// 한글: 호출자 스레드를 제외한 모든 스레드 일시 중단 / 재개
	static void SuspendOtherThreads();
	static void ResumeOtherThreads();

	// English: Map exception code to readable string
	// 한글: 예외 코드를 가독성 있는 문자열로 변환
	static const char* ExceptionCodeToString(DWORD code);

	// English: Extract file name (strip directory path)
	// 한글: 파일명만 추출 (디렉토리 경로 제거)
	static void GetBaseFileName(const char* fullPath, char* outName, size_t outSize);

	// English: Dump output directory (set by Initialize)
	// 한글: 덤프 출력 디렉토리 (Initialize에서 설정)
	inline static char sDumpDir[MAX_PATH] = {};

	// English: Suspended thread handles collected in SuspendOtherThreads
	// 한글: SuspendOtherThreads에서 수집된 일시 중단된 스레드 핸들
	inline static HANDLE sSuspendedHandles[2048] = {};
	inline static DWORD  sSuspendedCount         = 0;
};

} // namespace Network::Utils

#endif // _WIN32
