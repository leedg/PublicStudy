#pragma once

// Structured async scope (task tracking + cooperative cancel).

#include "KeyedDispatcher.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
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

	// Reset for pool reuse. Safe only when mInFlight == 0.
	//          Callers MUST call WaitForDrain(-1) (via Session::WaitForPendingTasks())
	//          before calling Reset(). SessionPool::ReleaseInternal enforces this order:
	//            Close() → WaitForPendingTasks() → Reset()
	//          The assert below fires immediately if the contract is violated.
	//         Close() → WaitForPendingTasks() → Reset()
	void Reset()
	{
		// Precondition: mInFlight MUST be 0. Call WaitForDrain(-1) before Reset().
		//          Violating this corrupts the in-flight counter of the reused scope.
		assert(mInFlight.load(std::memory_order_acquire) == 0 &&
		       "AsyncScope::Reset() called while tasks are in-flight");
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

		// Fast-path: if already cancelled, avoid dispatching a no-op task.
		//          A concurrent Cancel() between here and Dispatch() is still safe —
		//          the wrapped lambda checks IsCancelled() at execution time.
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
			// notify_all does not require the mutex to be held.
			//          Acquiring it here would add unnecessary contention with WaitForDrain.
			mDrainCV.notify_all();
		}
	}

	std::atomic<bool> mCancelled;
	std::atomic<size_t> mInFlight;
	// mDrainMutex exists solely to satisfy the std::condition_variable contract —
	//          condition_variable::wait requires a unique_lock even when no shared mutable
	//          state needs protection (mInFlight is atomic and safe to read without a lock).
	//          EndTask calls notify_all WITHOUT holding this mutex; that is intentional and
	//          correct: notify_all does not need the mutex, and acquiring it would add
	//          unnecessary contention on the hot EndTask path.
	mutable std::mutex mDrainMutex;
	std::condition_variable mDrainCV;
};

} // namespace Network::Concurrency
