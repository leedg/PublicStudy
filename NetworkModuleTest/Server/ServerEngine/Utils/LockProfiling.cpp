#include "LockProfiling.h"

#if defined(NET_LOCK_PROFILING)

#include <cstdlib>
#include <mutex>

#ifdef _WIN32
#include <TraceLoggingProvider.h>
#pragma comment(lib, "advapi32.lib")

TRACELOGGING_DEFINE_PROVIDER(
	g_NetworkLockProfilingProvider,
	"NetworkModule.LockProfiling",
	(0x6f1c2b17, 0x8c9b, 0x4db1, 0x9a, 0x2d, 0x5f, 0x83, 0x2f, 0x1e, 0x2a, 0x91));

namespace Network::Utils::LockProfiling
{
namespace
{
	std::once_flag g_registerOnce;

	void RegisterProvider() noexcept
	{
		TraceLoggingRegister(g_NetworkLockProfilingProvider);
		std::atexit([]() { TraceLoggingUnregister(g_NetworkLockProfilingProvider); });
	}

	void EnsureRegistered() noexcept
	{
		std::call_once(g_registerOnce, RegisterProvider);
	}
} // namespace

void EmitLockRecord(const LockRecord &record) noexcept
{
	EnsureRegistered();
	// English: TraceLogging buffers may drop records if the buffer is full.
	// This is a known limitation of the Windows TraceLogging API - when the
	// internal buffer reaches capacity, records are silently discarded to avoid
	// blocking the caller. Profiling data may be incomplete if lock activity is heavy.
	// 한글: TraceLogging 버퍼가 가득 찬 경우 기록이 드롭될 수 있습니다.
	// 이는 Windows TraceLogging API의 알려진 제한 사항으로, 내부 버퍼가
	// 용량에 도달할 때 호출자를 차단하지 않기 위해 레코드가 자동으로 삭제됩니다.
	// 락 활동이 많은 경우 프로파일링 데이터가 불완전할 수 있습니다.
	TraceLoggingWrite(g_NetworkLockProfilingProvider,
					  "LockScope",
					  TraceLoggingString(record.name, "Name"),
					  TraceLoggingString(record.file, "File"),
					  TraceLoggingInt32(record.line, "Line"),
					  TraceLoggingUInt64(record.waitNs, "WaitNs"),
					  TraceLoggingUInt64(record.holdNs, "HoldNs"),
					  TraceLoggingUInt32(record.threadId, "ThreadId"));
}

} // namespace Network::Utils::LockProfiling

#else

namespace Network::Utils::LockProfiling
{
void EmitLockRecord(const LockRecord &record) noexcept { (void)record; }
} // namespace Network::Utils::LockProfiling

#endif // _WIN32

#endif // NET_LOCK_PROFILING
