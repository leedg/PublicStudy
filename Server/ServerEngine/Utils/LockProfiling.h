#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <type_traits>

// Lock contention profiling
// Enable by defining NET_LOCK_PROFILING in your build.
// When disabled, all macros compile to standard lock types with zero runtime cost.

#define NET_LOCK_PROFILE_CONCAT_INNER(a, b) a##b
#define NET_LOCK_PROFILE_CONCAT(a, b) NET_LOCK_PROFILE_CONCAT_INNER(a, b)
#define NET_LOCK_PROFILE_UNIQUE(base) NET_LOCK_PROFILE_CONCAT(base, __COUNTER__)

#if defined(NET_LOCK_PROFILING)

#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Network::Utils::LockProfiling
{
using Clock = std::chrono::steady_clock;

struct LockRecord
{
	const char *name;
	const char *file;
	int line;
	uint64_t waitNs;
	uint64_t holdNs;
	uint32_t threadId;
};

void EmitLockRecord(const LockRecord &record) noexcept;

inline uint64_t ToNs(Clock::duration duration) noexcept
{
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

inline uint32_t GetThreadId() noexcept
{
#ifdef _WIN32
	return static_cast<uint32_t>(::GetCurrentThreadId());
#else
	return static_cast<uint32_t>(
		std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

struct LockTiming
{
	const char *name;
	const char *file;
	int line;
	Clock::time_point waitStart;
	Clock::time_point acquired;
};

inline LockTiming StartLockWait(const char *name, const char *file,
											int line) noexcept
{
	return LockTiming{name, file, line, Clock::now(), Clock::time_point{}};
}

inline void MarkLockAcquired(LockTiming &timing) noexcept
{
	timing.acquired = Clock::now();
}

class LockHoldScope
{
  public:
	explicit LockHoldScope(LockTiming &timing) noexcept : mTiming(timing) {}

	~LockHoldScope() noexcept
	{
		const auto end = Clock::now();
		const LockRecord record{mTiming.name,
							mTiming.file,
							mTiming.line,
							ToNs(mTiming.acquired - mTiming.waitStart),
							ToNs(end - mTiming.acquired),
							GetThreadId()};
		EmitLockRecord(record);
	}

	LockHoldScope(const LockHoldScope &) = delete;
	LockHoldScope &operator=(const LockHoldScope &) = delete;

  private:
	LockTiming &mTiming;
};

template <typename Mutex>
class LockGuard
{
  public:
	LockGuard(Mutex &mutex, const char *name, const char *file, int line)
		: mMutex(mutex), mTiming(StartLockWait(name, file, line))
	{
		mMutex.lock();
		MarkLockAcquired(mTiming);
	}

	~LockGuard() noexcept
	{
		const auto end = Clock::now();
		const LockRecord record{mTiming.name,
							mTiming.file,
							mTiming.line,
							ToNs(mTiming.acquired - mTiming.waitStart),
							ToNs(end - mTiming.acquired),
							GetThreadId()};
		EmitLockRecord(record);
		mMutex.unlock();
	}

	LockGuard(const LockGuard &) = delete;
	LockGuard &operator=(const LockGuard &) = delete;

  private:
	Mutex &mMutex;
	LockTiming mTiming;
};

} // namespace Network::Utils::LockProfiling

#define NET_LOCK_GUARD_NAMED(mutex, name)                                                     \
	Network::Utils::LockProfiling::LockGuard<                            \
			std::remove_reference_t<decltype(mutex)>>                       \
		name(mutex, #mutex, __FILE__, __LINE__)

#define NET_LOCK_GUARD(mutex)                                                                \
	NET_LOCK_GUARD_NAMED(mutex, NET_LOCK_PROFILE_UNIQUE(_net_lock_))

#define NET_UNIQUE_LOCK_NAMED(mutex, name)                                                    \
	NET_UNIQUE_LOCK_IMPL(mutex, name, NET_LOCK_PROFILE_UNIQUE(_net_lock_timing_))

#define NET_UNIQUE_LOCK(mutex) NET_UNIQUE_LOCK_NAMED(mutex, lock)

#define NET_UNIQUE_LOCK_IMPL(mutex, name, timingVar)                                          \
	std::unique_lock<std::mutex> name(mutex, std::defer_lock);                                  \
	auto timingVar = Network::Utils::LockProfiling::StartLockWait(#mutex, __FILE__, __LINE__);  \
	name.lock();                                                                                \
	Network::Utils::LockProfiling::MarkLockAcquired(timingVar);                                 \
	Network::Utils::LockProfiling::LockHoldScope                                                \
		NET_LOCK_PROFILE_CONCAT(timingVar, _hold)(timingVar)

#else

#define NET_LOCK_GUARD_NAMED(mutex, name) \
	std::lock_guard<std::remove_reference_t<decltype(mutex)>> name((mutex))

#define NET_LOCK_GUARD(mutex) \
	std::lock_guard<std::remove_reference_t<decltype(mutex)>> NET_LOCK_PROFILE_UNIQUE(_net_lock_)((mutex))

#define NET_UNIQUE_LOCK_NAMED(mutex, name) std::unique_lock<std::mutex> name(mutex)
#define NET_UNIQUE_LOCK(mutex) std::unique_lock<std::mutex> lock(mutex)

#endif // NET_LOCK_PROFILING
