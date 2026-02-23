#pragma once

// English: Structured async scope (task tracking + cooperative cancel).
// 한글: 구조화된 비동기 스코프(태스크 추적 + 협력 취소).

#include "KeyedDispatcher.h"
#include <atomic>
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

	bool IsCancelled() const
	{
		return mCancelled.load(std::memory_order_acquire);
	}

	template <typename Fn>
	bool Submit(KeyedDispatcher &dispatcher, uint64_t key, Fn &&task, int timeoutMs = -1)
	{
		BeginTask();

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
			std::lock_guard<std::mutex> lock(mDrainMutex);
			mDrainCV.notify_all();
		}
	}

	std::atomic<bool> mCancelled;
	std::atomic<size_t> mInFlight;
	mutable std::mutex mDrainMutex;
	std::condition_variable mDrainCV;
};

} // namespace Network::Concurrency
