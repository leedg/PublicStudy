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
