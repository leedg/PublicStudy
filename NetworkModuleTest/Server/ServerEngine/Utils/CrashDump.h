// Crash dump utility — Windows SEH / signal handler that writes
//          .dmp (mini + full) and .crash (text callstack) on unhandled exception.
//          Self-contained: no external project dependencies.
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
	// Call once at startup (before spawning threads).
	//          dumpDir: directory for output files, e.g. "./dumps/"
	//                   nullptr or "" → current directory.
	static void Initialize(const char* dumpDir = nullptr);

private:
	// SEH unhandled-exception filter — entry point
	static LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS* exceptionInfo);

	// Noop filter — installed after first exception to prevent re-entry
	static LONG WINAPI ExceptionFilterNoop(EXCEPTION_POINTERS* exceptionInfo);

	// Actual dump work runs in a separate thread
	//          (avoids stack-overflow issues on the crashed thread's stack)
	static DWORD WINAPI ExceptionProc(void* arg);

	// Write mini/full .dmp file via MiniDumpWriteDump
	static DWORD WriteMiniDump(EXCEPTION_POINTERS* exceptionInfo,
	                           const char*          dumpName,
	                           MINIDUMP_TYPE        dumpType);

	// Write registers + StackWalk64 callstack to wofstream
	static void WriteCallStack(std::wofstream& outFile,
	                           HANDLE          threadHandle,
	                           CONTEXT*        context,
	                           bool            isCrashed);

	// Suspend / resume all threads except the caller
	static void SuspendOtherThreads();
	static void ResumeOtherThreads();

	// Map exception code to readable string
	static const char* ExceptionCodeToString(DWORD code);

	// Extract file name (strip directory path)
	static void GetBaseFileName(const char* fullPath, char* outName, size_t outSize);

	// Dump output directory (set by Initialize)
	inline static char sDumpDir[MAX_PATH] = {};

	// Suspended thread handles collected in SuspendOtherThreads
	inline static HANDLE sSuspendedHandles[2048] = {};
	inline static DWORD  sSuspendedCount         = 0;
};

} // namespace Network::Utils

#endif // _WIN32
