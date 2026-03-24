#pragma once

// 락 경합 프로파일링 유틸리티.
//
// 활성화: 빌드에 NET_LOCK_PROFILING 매크로를 정의한다.
// 비활성화 시: 모든 매크로가 표준 std::lock_guard / std::unique_lock으로
//   컴파일되며 런타임 오버헤드는 0이다.
//
// 측정 방식:
//   - steady_clock으로 lock() 호출 직전(waitStart)과 직후(acquired)를 기록한다.
//   - 스코프 소멸 시 acquired~소멸 시점을 holdNs로 산출한다.
//   - 측정값은 EmitLockRecord()를 통해 Windows TraceLogging 이벤트로 내보낸다.
//   - TraceLogging 수신기(WPR, PerfView 등)로 분석하거나 CSV로 내보낼 수 있다.
//
// 오버헤드 트레이드오프:
//   - steady_clock 호출 2회(wait 시작, 획득 완료) + 1회(해제) 추가 = ~수십 ns/락
//   - TraceLogging 버퍼가 가득 차면 레코드가 무음 드롭될 수 있다.
//     (호출자를 블록하지 않는 설계이므로 고빈도 락에서 데이터 손실 가능)
//   - 프로파일링 빌드에서만 활성화하고 릴리즈 빌드에는 적용하지 말 것.

#include <chrono>
#include <cstdint>
#include <mutex>
#include <type_traits>

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

// 단일 락 획득/해제 이벤트 데이터.
// EmitLockRecord()가 이 구조체를 TraceLogging 이벤트로 직렬화한다.
struct LockRecord
{
	const char *name;      // 락 변수 이름 (문자열 리터럴 — 생존 기간 보장 필요)
	const char *file;      // 소스 파일 경로 (__FILE__ 리터럴)
	int         line;      // 소스 라인 번호 (__LINE__)
	uint64_t    waitNs;    // lock() 호출~획득까지 대기 시간 (나노초)
	uint64_t    holdNs;    // 락 획득~해제까지 보유 시간 (나노초)
	uint32_t    threadId;  // 락을 획득한 스레드 ID
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
	const char       *name;      // 락 변수 이름 (LockRecord와 동일한 문자열 리터럴)
	const char       *file;      // 소스 파일 경로
	int               line;      // 소스 라인 번호
	Clock::time_point waitStart; // lock() 호출 직전 시각 — 대기 시간 산출 기준
	Clock::time_point acquired;  // lock() 반환 직후 시각 — 보유 시간 산출 기준
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

// RAII 스코프 종료 시 holdNs를 산출하고 EmitLockRecord()를 호출한다.
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
	LockTiming &mTiming;  // 소멸자에서 holdNs 계산에 사용할 타이밍 참조
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
	Mutex     &mMutex;   // 보호 대상 뮤텍스 참조 — 소멸자에서 unlock() 호출
	LockTiming mTiming;  // 대기/보유 시간 측정용 타이밍 스냅샷
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

// 프로파일링 비활성화 시 — 표준 락으로 무비용 교체
#define NET_LOCK_GUARD_NAMED(mutex, name) \
	std::lock_guard<std::remove_reference_t<decltype(mutex)>> name((mutex))

#define NET_LOCK_GUARD(mutex) \
	std::lock_guard<std::remove_reference_t<decltype(mutex)>> NET_LOCK_PROFILE_UNIQUE(_net_lock_)((mutex))

#define NET_UNIQUE_LOCK_NAMED(mutex, name) std::unique_lock<std::mutex> name(mutex)
#define NET_UNIQUE_LOCK(mutex) std::unique_lock<std::mutex> lock(mutex)

#endif // NET_LOCK_PROFILING
