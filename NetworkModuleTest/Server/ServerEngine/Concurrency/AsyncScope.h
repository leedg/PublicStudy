#pragma once

// English: Structured async scope (task tracking + cooperative cancel).
// 한글: 구조화된 비동기 스코프(태스크 추적 + 협력 취소).

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

	// English: Reset for pool reuse. Safe only when mInFlight == 0.
	//          Callers MUST call WaitForDrain(-1) (via Session::WaitForPendingTasks())
	//          before calling Reset(). SessionPool::ReleaseInternal enforces this order:
	//            Close() → WaitForPendingTasks() → Reset()
	//          The assert below fires immediately if the contract is violated.
	// 한글: 풀 재사용을 위한 초기화. mInFlight == 0일 때만 안전.
	//       호출자는 Reset() 전에 WaitForDrain(-1) (Session::WaitForPendingTasks() 경유)을
	//       반드시 호출해야 함. SessionPool::ReleaseInternal이 이 순서를 강제:
	//         Close() → WaitForPendingTasks() → Reset()
	//       계약 위반 시 아래 assert가 즉시 발동.
	void Reset()
	{
		// English: Precondition: mInFlight MUST be 0. Call WaitForDrain(-1) before Reset().
		//          Violating this corrupts the in-flight counter of the reused scope.
		//          Debug builds: assert fires immediately for fast diagnosis in the debugger.
		//          Release builds: explicit abort — corruption is not recoverable, and
		//          silently continuing would cause hard-to-diagnose memory safety bugs.
		// 한글: 선행 조건: mInFlight가 반드시 0이어야 함. Reset() 전 WaitForDrain(-1) 필수.
		//       위반 시 재사용 스코프의 in-flight 카운터 오염.
		//       디버그: assert가 즉시 발동하여 디버거에서 빠른 진단 가능.
		//       릴리즈: 오염은 복구 불가 — 조용히 계속하면 진단 어려운 메모리 안전성 버그 발생.
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

		// English: Fast-path: if already cancelled, avoid dispatching a no-op task.
		//          A concurrent Cancel() between here and Dispatch() is still safe —
		//          the wrapped lambda checks IsCancelled() at execution time.
		// 한글: 패스트패스: 이미 취소된 경우 no-op 태스크 디스패치를 피함.
		//       BeginTask와 Dispatch 사이의 동시 Cancel()은 안전 —
		//       래퍼 람다가 실행 시점에 IsCancelled()를 재확인함.
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
			// English: notify_all does not require the mutex to be held.
			//          Acquiring it here would add unnecessary contention with WaitForDrain.
			// 한글: notify_all은 mutex를 보유할 필요가 없음.
			//       lock을 잡으면 WaitForDrain과 불필요한 contention 발생.
			mDrainCV.notify_all();
		}
	}

	std::atomic<bool> mCancelled;
	std::atomic<size_t> mInFlight;
	// English: mDrainMutex exists solely to satisfy the std::condition_variable contract —
	//          condition_variable::wait requires a unique_lock even when no shared mutable
	//          state needs protection (mInFlight is atomic and safe to read without a lock).
	//          EndTask calls notify_all WITHOUT holding this mutex; that is intentional and
	//          correct: notify_all does not need the mutex, and acquiring it would add
	//          unnecessary contention on the hot EndTask path.
	// 한글: mDrainMutex는 순전히 std::condition_variable 계약을 충족하기 위해 존재 —
	//       condition_variable::wait는 보호할 공유 가변 상태가 없더라도 unique_lock을 요구
	//       (mInFlight는 atomic이라 락 없이 안전하게 읽을 수 있음).
	//       EndTask는 이 mutex를 보유하지 않고 notify_all을 호출하며, 이는 의도적이고 정확함:
	//       notify_all은 mutex가 필요 없고, lock을 잡으면 핫 패스인 EndTask에 불필요한
	//       contention이 발생함.
	mutable std::mutex mDrainMutex;
	std::condition_variable mDrainCV;
};

} // namespace Network::Concurrency
