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
		// English: Route to backend-specific wait to prevent missed-notification race.
		//          Mutex backend: CV must be waited under mMutexQueueMutex (same mutex
		//          the producer holds during push+notify) so notify cannot slip between
		//          the consumer's empty-check and its wait registration.
		//          Lock-free backend: CV is waited under mWaitMutex; producer must
		//          notify while holding mWaitMutex (see TryPushLockFree).
		// 한글: missed-notification 경쟁을 방지하기 위해 백엔드별 대기 경로 분기.
		//       Mutex 백엔드: CV를 mMutexQueueMutex 하에서 대기해야 함. 생산자가
		//       push+notify 시 동일 뮤텍스를 보유하므로 비어있음 확인과 대기 등록
		//       사이에 notify가 끼어들 수 없음.
		//       Lock-free 백엔드: CV를 mWaitMutex 하에서 대기; 생산자는
		//       mWaitMutex 보유 중에 notify 해야 함 (TryPushLockFree 참조).
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
		// English: Notify under mWaitMutex to prevent missed-notification race with
		//          PopLockFreeWait's predicate check + wait (both under mWaitMutex).
		// 한글: PopLockFreeWait의 조건 확인+대기(둘 다 mWaitMutex 하에서)와의
		//       missed-notification 경쟁 방지를 위해 mWaitMutex 보유 중 notify.
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
				mNotFullLFCV.wait(lock, [this] {
					return mShutdown.load(std::memory_order_acquire) ||
						mSize.load(std::memory_order_acquire) < Capacity();
				});
			}
			else
			{
				// English: mSize is best-effort approximation (±1 possible due to lock-free design).
				//          Spurious wakeup is defended by retry loop above; TryPushLockFree re-validates.
				// 한글: mSize는 best-effort 근사값(±1 가능). spurious wakeup 후 TryPushLockFree에서 재검증함.
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
		mNotFullLFCV.notify_one();
		return true;
	}

	// English: Mutex-backend blocking wait for Pop().
	//          Waits mNotEmptyMutexCV under mMutexQueueMutex — the same mutex held by the
	//          producer during push, eliminating the missed-notification race that occurs
	//          when producer and consumer use different mutexes.
	// 한글: Mutex 백엔드용 블로킹 Pop() 대기.
	//       생산자가 push 시 보유하는 것과 동일한 뮤텍스(mMutexQueueMutex) 하에서
	//       mNotEmptyMutexCV를 대기하여, 서로 다른 뮤텍스 사용 시 발생하는
	//       missed-notification 경쟁을 제거함.
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
		// English: After shutdown, drain remaining items.
		// 한글: shutdown 이후 잔여 아이템 drain.
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

	// English: Lock-free-backend blocking wait for Pop().
	//          Waits mNotEmptyLFCV_pop under mWaitMutex; producer must notify under the same
	//          mutex (see TryPushLockFree) to prevent missed-notification race.
	// 한글: Lock-free 백엔드용 블로킹 Pop() 대기.
	//       mWaitMutex 하에서 mNotEmptyLFCV_pop을 대기; 생산자는 동일 뮤텍스 보유 중에
	//       notify 해야 함 (TryPushLockFree 참조).
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
		// English: After shutdown, only allow draining existing items.
		// 한글: shutdown 이후에는 잔여 아이템만 drain 허용.
		return TryPop(out);
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

	std::condition_variable mNotEmptyMutexCV;  // Mutex backend only (with mMutexQueueMutex)
	std::condition_variable mNotEmptyLFCV_pop; // LockFree backend Pop (with mWaitMutex)

	std::condition_variable mNotFullMutexCV;  // English: Used with mMutexQueueMutex (mutex backend)
                                              // 한글: 뮤텍스 백엔드용 — mMutexQueueMutex와 함께 사용
	std::condition_variable mNotFullLFCV;     // English: Used with mWaitMutex (lock-free backend)
                                              // 한글: lock-free 백엔드용 — mWaitMutex와 함께 사용
};

} // namespace Network::Concurrency

