#pragma once

// English: Unified execution queue with mutex/lock-free backends.
// 한글: mutex/lock-free 백엔드를 통합한 실행 큐.

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
// English: Queue backend and backpressure policy.
// 한글: 큐 백엔드 및 백프레셔 정책.
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
	size_t mCapacity = 0; // English: 0 = unbounded (Mutex backend only)
						 // 한글: 0 = 무제한 (Mutex 백엔드에서만 허용)
};

// =============================================================================
// English: ExecutionQueue
// - TryPush/TryPop are always non-blocking.
// - Push/Pop can block depending on policy and timeout.
//
// 한글: ExecutionQueue
// - TryPush/TryPop은 항상 논블로킹.
// - Push/Pop은 정책/timeout에 따라 블로킹 가능.
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
			// English: Lock-free backend must be bounded.
			// 한글: lock-free 백엔드는 고정 크기 필수.
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

		// English: Blocking mode.
		// 한글: 블로킹 모드.
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

		const auto deadline =
			(timeoutMs < 0)
				? std::chrono::steady_clock::time_point::max()
				: (std::chrono::steady_clock::now() +
				   std::chrono::milliseconds(timeoutMs));

		while (!mShutdown.load(std::memory_order_acquire))
		{
			std::unique_lock<std::mutex> lock(mWaitMutex);
			if (timeoutMs < 0)
			{
				mNotEmptyCV.wait(lock, [this] {
					return mShutdown.load(std::memory_order_acquire) ||
						   mSize.load(std::memory_order_acquire) > 0;
				});
			}
			else
			{
				if (!mNotEmptyCV.wait_until(lock, deadline, [this] {
						return mShutdown.load(std::memory_order_acquire) ||
							   mSize.load(std::memory_order_acquire) > 0;
					}))
				{
					return false;
				}
			}

			if (TryPop(out))
			{
				return true;
			}

			if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}
		}

		// English: After shutdown, only allow draining existing items.
		// 한글: shutdown 이후에는 잔여 아이템만 drain 허용.
		return TryPop(out);
	}

	void Shutdown()
	{
		bool expected = false;
		if (!mShutdown.compare_exchange_strong(
				expected, true, std::memory_order_acq_rel))
		{
			return;
		}

		mNotEmptyCV.notify_all();
		mNotFullCV.notify_all();
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

		mNotEmptyCV.notify_one();
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
		mNotEmptyCV.notify_one();
		return true;
	}

	bool PushMutexBlocking(T &&value, int timeoutMs)
	{
		std::unique_lock<std::mutex> lock(mMutexQueueMutex);

		if (timeoutMs < 0)
		{
			mNotFullCV.wait(lock, [this] {
				return mShutdown.load(std::memory_order_acquire) ||
					   mOptions.mCapacity == 0 ||
					   mMutexQueue.size() < mOptions.mCapacity;
			});
		}
		else
		{
			const auto deadline =
				std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
			if (!mNotFullCV.wait_until(lock, deadline, [this] {
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

		mNotEmptyCV.notify_one();
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
				? std::chrono::steady_clock::time_point::max()
				: (std::chrono::steady_clock::now() +
				   std::chrono::milliseconds(timeoutMs));

		// English: Retain value until enqueue succeeds; do not move before confirmed success.
		// 한글: enqueue 성공 전까지 value를 보유. move는 성공 직전 한 번만 수행.
		// [Fix A-1] 루프마다 value의 복사본을 TryPushLockFree에 넘긴다.
		// 원본 value는 루프 전체 기간 동안 온전히 유지되어야 한다.
		// std::move(value)를 루프 안에서 직접 쓰면 첫 번째 시도 실패 후
		// value가 moved-from 상태가 되어 두 번째 반복에서 use-after-move가 발생한다.
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
				// English: mSize is best-effort approximation (±1 possible due to lock-free design).

				//          Spurious wakeup is defended by retry loop above; TryPushLockFree re-validates.

				// 한글: mSize는 best-effort 근사값(±1 가능). spurious wakeup 후 TryPushLockFree에서 재검증함.

				mNotFullCV.wait(lock, [this] {

					return mShutdown.load(std::memory_order_acquire) ||

						mSize.load(std::memory_order_acquire) < Capacity();
				});
			}
			else
			{
				// English: mSize is best-effort approximation (±1 possible due to lock-free design).

				//          Spurious wakeup is defended by retry loop above; TryPushLockFree re-validates.

				// 한글: mSize는 best-effort 근사값(±1 가능). spurious wakeup 후 TryPushLockFree에서 재검증함.

				if (!mNotFullCV.wait_until(lock, deadline, [this] {

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

		mNotFullCV.notify_one();
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
		mNotFullCV.notify_one();
		return true;
	}

	ExecutionQueueOptions<T> mOptions;
	std::atomic<bool> mShutdown;
	// English: mSize is a best-effort approximation for the lock-free backend.
	//          fetch_add/fetch_sub are not atomic with TryEnqueue/TryDequeue,
	//          so Size() may transiently deviate by ±1 under high concurrency.
	//          Do not use for correctness decisions; use for monitoring only.
	// 한글: lock-free 백엔드에서 mSize는 최선 근사값(best-effort).
	//       TryEnqueue/TryDequeue와 fetch_add/fetch_sub 간 원자성이 없으므로
	//       고경합 시 ±1 오차 발생 가능. 모니터링 용도로만 사용.
	std::atomic<size_t> mSize;

	// English: Mutex backend state.
	// 한글: Mutex 백엔드 상태.
	std::queue<T> mMutexQueue;
	mutable std::mutex mMutexQueueMutex;

	// English: Lock-free backend state.
	// 한글: Lock-free 백엔드 상태.
	std::unique_ptr<BoundedLockFreeQueue<T>> mLockFreeQueue;

	// English: Waiting/notification state.
	// 한글: 대기/신호 상태.
	mutable std::mutex mWaitMutex;
	std::condition_variable mNotEmptyCV;
	std::condition_variable mNotFullCV;
};

} // namespace Network::Concurrency
