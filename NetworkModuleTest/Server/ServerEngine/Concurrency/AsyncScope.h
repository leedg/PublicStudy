#pragma once

// 구조화된 비동기 스코프: 태스크 인플라이트 추적 + 협력 취소.

#include "KeyedDispatcher.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <utility>

namespace Network::Concurrency
{
class AsyncScope
{
  public:
	AsyncScope()
		: mCancelled(false),
		  mInFlight(0)
	{
	}

	~AsyncScope()
	{
		Cancel();
		WaitForDrain(-1);
	}

	AsyncScope(const AsyncScope &) = delete;
	AsyncScope &operator=(const AsyncScope &) = delete;

	void Cancel()
	{
		mCancelled.store(true, std::memory_order_release);
	}

	// 풀 재사용을 위한 초기화. mInFlight == 0 일 때만 안전하다.
	//
	// 호출 순서 계약 (SessionPool::ReleaseInternal이 강제):
	//   Close() → WaitForPendingTasks() [= WaitForDrain(-1)] → Reset()
	//
	// Reset()을 WaitForDrain() 없이 호출하면 인플라이트 카운터가 오염되어,
	// 이후 WaitForDrain()이 영구 대기하거나 태스크가 해제된 메모리에 접근한다.
	// 디버그 빌드: assert가 즉시 발동하여 디버거에서 빠른 진단이 가능하다.
	// 릴리즈 빌드: abort로 즉시 종료한다. 오염된 상태로 계속 실행하면
	//              재현하기 어려운 메모리 안전성 버그로 이어지기 때문이다.
	void Reset()
	{
		const size_t inFlight = mInFlight.load(std::memory_order_acquire);
		assert(inFlight == 0 &&
		       "AsyncScope::Reset() called while tasks are in-flight");
		if (inFlight != 0)
		{
			std::fprintf(stderr,
			    "[AsyncScope::Reset] FATAL: in-flight count is %zu (expected 0). "
			    "Ensure WaitForDrain(-1) is called before Reset().\n",
			    inFlight);
			std::abort();
		}
		mCancelled.store(false, std::memory_order_release);
	}

	bool IsCancelled() const
	{
		return mCancelled.load(std::memory_order_acquire);
	}

	template <typename Fn>
	bool Submit(KeyedDispatcher &dispatcher, uint64_t key, Fn &&task, int timeoutMs = -1)
	{
		BeginTask();

		// 패스트패스: 이미 취소된 경우 no-op 태스크 디스패치를 피한다.
		// BeginTask()와 Dispatch() 사이의 동시 Cancel()은 안전하다.
		// 래퍼 람다가 실행 시점에 IsCancelled()를 재확인하기 때문이다.
		if (IsCancelled())
		{
			EndTask();
			return false;
		}

		auto wrapped = [this, fn = std::forward<Fn>(task)]() mutable {
			if (!IsCancelled())
			{
				fn();
			}
			EndTask();
		};

		if (!dispatcher.Dispatch(key, std::move(wrapped), timeoutMs))
		{
			EndTask();
			return false;
		}

		return true;
	}

	bool WaitForDrain(int timeoutMs)
	{
		std::unique_lock<std::mutex> lock(mDrainMutex);
		if (timeoutMs < 0)
		{
			// 람다 조건식이 spurious wakeup을 방어한다.
			// OS는 notify 없이도 wait를 깨울 수 있으므로,
			// 람다가 false를 반환하면 wait가 자동으로 재대기한다.
			mDrainCV.wait(lock, [this] {
				return mInFlight.load(std::memory_order_acquire) == 0;
			});
			return true;
		}

		const auto deadline =
			std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
		return mDrainCV.wait_until(lock, deadline, [this] {
			return mInFlight.load(std::memory_order_acquire) == 0;
		});
	}

	size_t InFlightCount() const
	{
		return mInFlight.load(std::memory_order_acquire);
	}

  private:
	void BeginTask()
	{
		mInFlight.fetch_add(1, std::memory_order_acq_rel);
	}

	void EndTask()
	{
		const size_t prev = mInFlight.fetch_sub(1, std::memory_order_acq_rel);
		if (prev == 1)
		{
			// notify_all은 mutex를 보유하지 않아도 된다.
			// mDrainMutex를 잡으면 WaitForDrain()과 불필요한 contention이 발생하고,
			// EndTask는 태스크 완료마다 호출되는 핫 패스이므로 contention을 최소화해야 한다.
			mDrainCV.notify_all();
		}
	}

	std::atomic<bool> mCancelled;
	std::atomic<size_t> mInFlight;

	// mDrainMutex는 순전히 std::condition_variable 계약을 충족하기 위해 존재한다.
	// condition_variable::wait는 보호할 공유 가변 상태가 없더라도 unique_lock을 요구한다.
	// (mInFlight는 atomic이므로 락 없이 안전하게 읽을 수 있다.)
	// EndTask는 이 mutex를 보유하지 않고 notify_all을 호출하며, 이는 의도적이고 정확하다.
	mutable std::mutex mDrainMutex;
	std::condition_variable mDrainCV;
};

} // namespace Network::Concurrency
