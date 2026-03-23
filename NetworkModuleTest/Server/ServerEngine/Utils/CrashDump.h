// 크래시 덤프 유틸리티 (Windows 전용).
//
// 동작 원리:
//   SetUnhandledExceptionFilter()로 SEH 미처리 예외를 가로채고,
//   충돌 발생 시 다음 두 종류의 파일을 생성한다:
//     - <exe>.<날짜>_mini.dmp : MiniDumpWriteDump (MiniDumpWithDataSegs 등)
//     - <exe>.<날짜>_full.dmp : MiniDumpWithFullMemory (전체 메모리 포함)
//     - <exe>.<날짜>.crash    : 텍스트 콜스택 (레지스터 + StackWalk64)
//
//   덤프 작업은 새 스레드에서 실행된다 — EXCEPTION_STACK_OVERFLOW 시
//   충돌 스레드의 스택을 재사용하면 2차 크래시가 발생하므로 분리가 필수.
//
//   SIGABRT/SIGFPE/SIGILL/SIGSEGV 및 순수 가상 호출/잘못된 매개변수도
//   SEH 필터를 경유하도록 핸들러를 등록한다.
//
// 사용법:
//   스레드 생성 전 main() 시작 직후에 한 번만 호출한다.
//   CrashDump::Initialize("./dumps/");
//
// 외부 의존성: 없음 (DbgHelp.lib, Psapi.lib는 #pragma comment로 자동 링크)
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
	// 스레드 생성 전 startup 시 1회 호출.
	// dumpDir: 출력 파일 저장 디렉토리 (nullptr 또는 "" → 현재 디렉토리)
	static void Initialize(const char* dumpDir = nullptr);

private:
	// SEH 미처리 예외 필터 — 최초 진입점.
	// 재진입 방지를 위해 즉시 noop 필터로 교체한 뒤 덤프 스레드를 생성한다.
	static LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS* exceptionInfo);

	// 덤프 작업 중 다른 스레드가 충돌했을 때 무한 대기시키는 noop 필터.
	static LONG WINAPI ExceptionFilterNoop(EXCEPTION_POINTERS* exceptionInfo);

	// 실제 덤프 작업 (별도 스레드에서 실행).
	static DWORD WINAPI ExceptionProc(void* arg);

	// MiniDumpWriteDump로 .dmp 파일 생성.
	static DWORD WriteMiniDump(EXCEPTION_POINTERS* exceptionInfo,
	                           const char*          dumpName,
	                           MINIDUMP_TYPE        dumpType);

	// 레지스터 값과 StackWalk64 콜스택을 wofstream에 기록.
	static void WriteCallStack(std::wofstream& outFile,
	                           HANDLE          threadHandle,
	                           CONTEXT*        context,
	                           bool            isCrashed);

	// 일관된 콜스택 수집을 위해 호출자 스레드를 제외한 모든 스레드를 일시 중단/재개.
	static void SuspendOtherThreads();
	static void ResumeOtherThreads();

	// Windows 예외 코드를 가독성 있는 문자열로 변환 (예: EXCEPTION_ACCESS_VIOLATION).
	static const char* ExceptionCodeToString(DWORD code);

	// 전체 경로에서 파일명만 추출.
	static void GetBaseFileName(const char* fullPath, char* outName, size_t outSize);

	// Initialize()에서 설정한 덤프 출력 디렉토리.
	inline static char sDumpDir[MAX_PATH] = {};

	// SuspendOtherThreads()에서 수집한 일시 중단된 스레드 핸들 목록.
	// 최대 2048개 스레드까지 추적한다.
	inline static HANDLE sSuspendedHandles[2048] = {};
	inline static DWORD  sSuspendedCount         = 0;
};

} // namespace Network::Utils

#endif // _WIN32
