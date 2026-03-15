#pragma once

// Unified execution queue with mutex/lock-free backends.

#include "BoundedLockFreeQueue.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>

namespace Network::Concurrency
{
// =============================================================================
// Queue backend and backpressure policy.
// =============================================================================
enum class QueueBackend : uint8_t
{
	Mutex,
	LockFree,
};

enum class BackpressurePolicy : uint8_t
{
	RejectNewest,
	Block,
};

template <typename T>
struct ExecutionQueueOptions
{
	QueueBackend mBackend = QueueBackend::Mutex;

	BackpressurePolicy mBackpressure = BackpressurePolicy::RejectNewest;

	size_t mCapacity = 0; // 0 = unbounded (Mutex backend only)

};
// =============================================================================
// ExecutionQueue
// - TryPush/TryPop are always non-blocking.
// - Push/Pop can block depending on policy and timeout.
//
// =============================================================================
template <typename T>
class ExecutionQueue
{
  public:

	explicit ExecutionQueue(const ExecutionQueueOptions<T> &options)

		: mOptions(options),
		  mShutdown(false),
		  mSize(0)
	{
		if (mOptions.mBackend == QueueBackend::LockFree)
		{
			// Lock-free backend must be bounded.
			const size_t capacity = (mOptions.mCapacity == 0) ? 1024 : mOptions.mCapacity;
			mLockFreeQueue = std::make_unique<BoundedLockFreeQueue<T>>(capacity);
		}
	}

	ExecutionQueue(const ExecutionQueue &) = delete;

	ExecutionQueue &operator=(const ExecutionQueue &) = delete;

	bool TryPush(const T &value)
	{
		T copy(value);
		return TryPush(std::move(copy));
	}

	bool TryPush(T &&value)
	{
		if (mShutdown.load(std::memory_order_acquire))
		{
			return false;
		}
		if (mOptions.mBackend == QueueBackend::Mutex)
		{
			return TryPushMutex(std::move(value));
		}
		return TryPushLockFree(std::move(value));
	}

	bool Push(const T &value, int timeoutMs = -1)
	{
		T copy(value);
		return Push(std::move(copy), timeoutMs);
	}

	bool Push(T &&value, int timeoutMs = -1)
	{
		if (mOptions.mBackpressure == BackpressurePolicy::RejectNewest)
		{
			return TryPush(std::move(value));
		}
		// Blocking mode.
		if (mOptions.mBackend == QueueBackend::Mutex)
		{
			return PushMutexBlocking(std::move(value), timeoutMs);
		}
		return PushLockFreeBlocking(std::move(value), timeoutMs);
	}

	bool TryPop(T &out)
	{
		if (mOptions.mBackend == QueueBackend::Mutex)
		{
			return TryPopMutex(out);
		}
		return TryPopLockFree(out);
	}

	bool Pop(T &out, int timeoutMs = -1)
	{
		if (TryPop(out))
		{
			return true;
		}
		if (timeoutMs == 0)
		{
			return false;
		}
		// Route to backend-specific wait to prevent missed-notification race.
		//          Mutex backend: CV must be waited under mMutexQueueMutex (same mutex
		//          the producer holds during push+notify) so notify cannot slip between
		//          the consumer's empty-check and its wait registration.
		//          Lock-free backend: CV is waited under mWaitMutex; producer must
		//          notify while holding mWaitMutex (see TryPushLockFree).
		if (mOptions.mBackend == QueueBackend::Mutex)
		{
			return PopMutexWait(out, timeoutMs);
		}
		return PopLockFreeWait(out, timeoutMs);
	}

	void Shutdown()
	{
		bool expected = false;
		if (!mShutdown.compare_exchange_strong(
				expected, true, std::memory_order_acq_rel))
		{
			return;
		}
		mNotEmptyMutexCV.notify_all();
		mNotEmptyLFCV_pop.notify_all();
		mNotFullMutexCV.notify_all();
		mNotFullLFCV.notify_all();
	}

	bool IsShutdown() const
	{
		return mShutdown.load(std::memory_order_acquire);
	}

	size_t Size() const
	{
		return mSize.load(std::memory_order_acquire);
	}

	bool Empty() const
	{
		return Size() == 0;
	}

	size_t Capacity() const
	{
		if (mOptions.mBackend == QueueBackend::LockFree && mLockFreeQueue)
		{
			return mLockFreeQueue->Capacity();
		}
		return mOptions.mCapacity;
	}

  private:

	bool TryPushMutex(T &&value)
	{
		{
			std::lock_guard<std::mutex> lock(mMutexQueueMutex);
			if (mShutdown.load(std::memory_order_acquire))
			{
				return false;
			}
			if (mOptions.mCapacity > 0 && mMutexQueue.size() >= mOptions.mCapacity)
			{
				return false;
			}
			mMutexQueue.push(std::move(value));
			mSize.fetch_add(1, std::memory_order_release);
		}
		mNotEmptyMutexCV.notify_one();
		return true;
	}

	bool TryPushLockFree(T &&value)
	{
		if (!mLockFreeQueue || mShutdown.load(std::memory_order_acquire))
		{
			return false;
		}
		if (!mLockFreeQueue->TryEnqueue(std::move(value)))
		{
			return false;
		}
		mSize.fetch_add(1, std::memory_order_release);
		// Notify under mWaitMutex to prevent missed-notification race with
		//          PopLockFreeWait's predicate check + wait (both under mWaitMutex).
		{
			std::lock_guard<std::mutex> wl(mWaitMutex);
			mNotEmptyLFCV_pop.notify_one();
		}
		return true;
	}

	bool PushMutexBlocking(T &&value, int timeoutMs)
	{
		std::unique_lock<std::mutex> lock(mMutexQueueMutex);
		if (timeoutMs < 0)
		{
			mNotFullMutexCV.wait(lock, [this] {
				return mShutdown.load(std::memory_order_acquire) ||
					   mOptions.mCapacity == 0 ||
					   mMutexQueue.size() < mOptions.mCapacity;
			});
		}
		else
		{
			const auto deadline =
				std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
			if (!mNotFullMutexCV.wait_until(lock, deadline, [this] {
					return mShutdown.load(std::memory_order_acquire) ||
						   mOptions.mCapacity == 0 ||
						   mMutexQueue.size() < mOptions.mCapacity;
				}))
			{
				return false;
			}
		}
		if (mShutdown.load(std::memory_order_acquire))
		{
			return false;
		}
		mMutexQueue.push(std::move(value));
		mSize.fetch_add(1, std::memory_order_release);
		lock.unlock();
		mNotEmptyMutexCV.notify_one();
		return true;
	}

	bool PushLockFreeBlocking(T &&value, int timeoutMs)
	{
		if (!mLockFreeQueue)
		{
			return false;
		}
		if (timeoutMs == 0)
		{
			return TryPushLockFree(std::move(value));
		}
		const auto deadline =
			(timeoutMs < 0)
				? (std::chrono::steady_clock::time_point::max)()
				: (std::chrono::steady_clock::now() +
				   std::chrono::milliseconds(timeoutMs));
		// Retain value until enqueue succeeds; do not move before confirmed success.

		// [Fix A-1] Pass a copy of value to TryPushLockFree each iteration.
		//          The original value must remain intact for the full duration of the loop.
		//          Using std::move(value) inside the loop causes use-after-move on the second
		//          iteration if the first attempt fails (value is left in a moved-from state).

		while (!mShutdown.load(std::memory_order_acquire))
		{
			T copy = value;
			if (TryPushLockFree(std::move(copy)))
			{
				return true;
			}
			if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}
			std::unique_lock<std::mutex> lock(mWaitMutex);
			if (timeoutMs < 0)
			{
				// mSize is best-effort approximation (±1 possible due to lock-free design).
				//          Spurious wakeup is defended by retry loop above; TryPushLockFree re-validates.
				mNotFullLFCV.wait(lock, [this] {
					return mShutdown.load(std::memory_order_acquire) ||
						mSize.load(std::memory_order_acquire) < Capacity();
				});
			}
			else
			{
				// mSize is best-effort approximation (±1 possible due to lock-free design).
				//          Spurious wakeup is defended by retry loop above; TryPushLockFree re-validates.
				if (!mNotFullLFCV.wait_until(lock, deadline, [this] {
						return mShutdown.load(std::memory_order_acquire) ||
							mSize.load(std::memory_order_acquire) < Capacity();
					}))
				{
					return false;
				}
			}
		}
		return false;
	}

	bool TryPopMutex(T &out)
	{
		{
			std::lock_guard<std::mutex> lock(mMutexQueueMutex);
			if (mMutexQueue.empty())
			{
				return false;
			}
			out = std::move(mMutexQueue.front());
			mMutexQueue.pop();
			mSize.fetch_sub(1, std::memory_order_release);
		}
		mNotFullMutexCV.notify_one();
		return true;
	}

	bool TryPopLockFree(T &out)
	{
		if (!mLockFreeQueue)
		{
			return false;
		}
		if (!mLockFreeQueue->TryDequeue(out))
		{
			return false;
		}
		mSize.fetch_sub(1, std::memory_order_release);
		// Notify under mWaitMutex to prevent missed-notification race with
		//          PushLockFreeBlocking's predicate check + wait (both under mWaitMutex).
		//          Without this guard, notify can fire between PushLockFreeBlocking's
		//          predicate-false observation and its wait() entry, causing the producer
		//          to block indefinitely (especially with timeoutMs < 0).
		{
			std::lock_guard<std::mutex> wl(mWaitMutex);
			mNotFullLFCV.notify_one();
		}
		return true;
	}

	// Mutex-backend blocking wait for Pop().
	//          Waits mNotEmptyMutexCV under mMutexQueueMutex — the same mutex held by the
	//          producer during push, eliminating the missed-notification race that occurs
	//          when producer and consumer use different mutexes.
	bool PopMutexWait(T &out, int timeoutMs)
	{
		const auto deadline =
			(timeoutMs < 0)
				? (std::chrono::steady_clock::time_point::max)()
				: (std::chrono::steady_clock::now() +
				   std::chrono::milliseconds(timeoutMs));
		while (!mShutdown.load(std::memory_order_acquire))
		{
			std::unique_lock<std::mutex> lock(mMutexQueueMutex);
			if (timeoutMs < 0)
			{
				mNotEmptyMutexCV.wait(lock, [this] {
					return mShutdown.load(std::memory_order_acquire) ||
						   !mMutexQueue.empty();
				});
			}
			else
			{
				if (!mNotEmptyMutexCV.wait_until(lock, deadline, [this] {
						return mShutdown.load(std::memory_order_acquire) ||
							   !mMutexQueue.empty();
					}))
				{
					return false;
				}
			}
			if (mShutdown.load(std::memory_order_acquire))
			{
				break;
			}
			if (!mMutexQueue.empty())
			{
				out = std::move(mMutexQueue.front());
				mMutexQueue.pop();
				mSize.fetch_sub(1, std::memory_order_release);
				lock.unlock();
				mNotFullMutexCV.notify_one();
				return true;
			}
		}
		// After shutdown, drain remaining items.
		std::lock_guard<std::mutex> lock(mMutexQueueMutex);
		if (!mMutexQueue.empty())
		{
			out = std::move(mMutexQueue.front());
			mMutexQueue.pop();
			mSize.fetch_sub(1, std::memory_order_release);
			return true;
		}
		return false;
	}

	// Lock-free-backend blocking wait for Pop().
	//          Waits mNotEmptyLFCV_pop under mWaitMutex; producer must notify under the same
	//          mutex (see TryPushLockFree) to prevent missed-notification race.
	bool PopLockFreeWait(T &out, int timeoutMs)
	{
		const auto deadline =
			(timeoutMs < 0)
				? (std::chrono::steady_clock::time_point::max)()
				: (std::chrono::steady_clock::now() +
				   std::chrono::milliseconds(timeoutMs));
		while (!mShutdown.load(std::memory_order_acquire))
		{
			std::unique_lock<std::mutex> lock(mWaitMutex);
			if (timeoutMs < 0)
			{
				mNotEmptyLFCV_pop.wait(lock, [this] {
					return mShutdown.load(std::memory_order_acquire) ||
						   mSize.load(std::memory_order_acquire) > 0;
				});
			}
			else
			{
				if (!mNotEmptyLFCV_pop.wait_until(lock, deadline, [this] {
						return mShutdown.load(std::memory_order_acquire) ||
							   mSize.load(std::memory_order_acquire) > 0;
					}))
				{
					return false;
				}
			}
			lock.unlock();
			if (TryPop(out))
			{
				return true;
			}
			if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}
		}
		// After shutdown, only allow draining existing items.
		return TryPop(out);
	}

	ExecutionQueueOptions<T> mOptions;

	std::atomic<bool> mShutdown;

	// mSize is a best-effort approximation for the lock-free backend.
	//          fetch_add/fetch_sub are not atomic with TryEnqueue/TryDequeue,
	//          so Size() may transiently deviate by ±1 under high concurrency.
	//          Do not use for correctness decisions; use for monitoring only.

	std::atomic<size_t> mSize;

	// Mutex backend state.

	std::queue<T> mMutexQueue;

	mutable std::mutex mMutexQueueMutex;

	// Lock-free backend state.

	std::unique_ptr<BoundedLockFreeQueue<T>> mLockFreeQueue;

	// Waiting/notification state.

	mutable std::mutex mWaitMutex;

	std::condition_variable mNotEmptyMutexCV;  // Mutex backend only (with mMutexQueueMutex)
	std::condition_variable mNotEmptyLFCV_pop; // LockFree backend Pop (with mWaitMutex)

	std::condition_variable mNotFullMutexCV;  // Used with mMutexQueueMutex (mutex backend)
	std::condition_variable mNotFullLFCV;     // Used with mWaitMutex (lock-free backend)
};

} // namespace Network::Concurrency

