#pragma once

// Mutex-backed execution queue with backpressure control.
// 한글: 백프레셔 제어를 지원하는 mutex 기반 실행 큐.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <utility>

namespace Network::Concurrency
{

enum class BackpressurePolicy : uint8_t
{
	RejectNewest,
	Block,
};

template <typename T>
struct ExecutionQueueOptions
{
	BackpressurePolicy mBackpressure = BackpressurePolicy::RejectNewest;

	size_t mCapacity = 0; // 0 = unbounded
	                      // 한글: 0 = 무제한
};

// =============================================================================
// ExecutionQueue
// - TryPush/TryPop are always non-blocking.
// - Push/Pop block when BackpressurePolicy::Block and the queue is full.
//
// 한글: ExecutionQueue
// - TryPush/TryPop은 항상 논블로킹.
// - Push/Pop은 BackpressurePolicy::Block이고 큐가 가득 찼을 때 블로킹.
// =============================================================================
template <typename T>
class ExecutionQueue
{
  public:

	explicit ExecutionQueue(const ExecutionQueueOptions<T> &options)
		: mOptions(options), mShutdown(false), mSize(0)
	{
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
			return false;
		return TryPushImpl(std::move(value));
	}

	bool Push(const T &value, int timeoutMs = -1)
	{
		T copy(value);
		return Push(std::move(copy), timeoutMs);
	}

	bool Push(T &&value, int timeoutMs = -1)
	{
		if (mOptions.mBackpressure == BackpressurePolicy::RejectNewest)
			return TryPush(std::move(value));
		return PushBlocking(std::move(value), timeoutMs);
	}

	bool TryPop(T &out)
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (mQueue.empty())
			return false;
		out = std::move(mQueue.front());
		mQueue.pop();
		mSize.fetch_sub(1, std::memory_order_release);
		mNotFullCV.notify_one();
		return true;
	}

	bool Pop(T &out, int timeoutMs = -1)
	{
		if (TryPop(out))
			return true;
		if (timeoutMs == 0)
			return false;
		return PopWait(out, timeoutMs);
	}

	void Shutdown()
	{
		bool expected = false;
		if (!mShutdown.compare_exchange_strong(
				expected, true, std::memory_order_acq_rel))
			return;
		mNotEmptyCV.notify_all();
		mNotFullCV.notify_all();
	}

	bool IsShutdown() const { return mShutdown.load(std::memory_order_acquire); }
	size_t Size()      const { return mSize.load(std::memory_order_acquire); }
	bool   Empty()     const { return Size() == 0; }
	size_t Capacity()  const { return mOptions.mCapacity; }

  private:

	bool TryPushImpl(T &&value)
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mShutdown.load(std::memory_order_acquire))
				return false;
			if (mOptions.mCapacity > 0 && mQueue.size() >= mOptions.mCapacity)
				return false;
			mQueue.push(std::move(value));
			mSize.fetch_add(1, std::memory_order_release);
		}
		mNotEmptyCV.notify_one();
		return true;
	}

	bool PushBlocking(T &&value, int timeoutMs)
	{
		std::unique_lock<std::mutex> lock(mMutex);
		auto canPush = [this] {
			return mShutdown.load(std::memory_order_acquire) ||
			       mOptions.mCapacity == 0 ||
			       mQueue.size() < mOptions.mCapacity;
		};
		if (timeoutMs < 0)
		{
			mNotFullCV.wait(lock, canPush);
		}
		else
		{
			const auto deadline = std::chrono::steady_clock::now() +
			                      std::chrono::milliseconds(timeoutMs);
			if (!mNotFullCV.wait_until(lock, deadline, canPush))
				return false;
		}
		if (mShutdown.load(std::memory_order_acquire))
			return false;
		mQueue.push(std::move(value));
		mSize.fetch_add(1, std::memory_order_release);
		lock.unlock();
		mNotEmptyCV.notify_one();
		return true;
	}

	// PopWait: waits mNotEmptyCV under mMutex — the same mutex held by the
	//          producer during push+notify — so notify cannot slip between the
	//          consumer's empty-check and its wait entry.
	// 한글: PopWait: 생산자가 push+notify 시 보유하는 것과 동일한 뮤텍스(mMutex) 하에서
	//       mNotEmptyCV를 대기하여 missed-notification 경쟁을 제거함.
	bool PopWait(T &out, int timeoutMs)
	{
		const auto deadline =
			std::chrono::steady_clock::now() +
			std::chrono::milliseconds(timeoutMs >= 0 ? timeoutMs : 0);
		while (!mShutdown.load(std::memory_order_acquire))
		{
			std::unique_lock<std::mutex> lock(mMutex);
			auto hasItem = [this] {
				return mShutdown.load(std::memory_order_acquire) ||
				       !mQueue.empty();
			};
			if (timeoutMs < 0)
				mNotEmptyCV.wait(lock, hasItem);
			else if (!mNotEmptyCV.wait_until(lock, deadline, hasItem))
				return false;

			if (mShutdown.load(std::memory_order_acquire))
				break;
			if (!mQueue.empty())
			{
				out = std::move(mQueue.front());
				mQueue.pop();
				mSize.fetch_sub(1, std::memory_order_release);
				lock.unlock();
				mNotFullCV.notify_one();
				return true;
			}
		}
		// After shutdown, drain remaining items.
		// 한글: shutdown 이후 잔여 아이템 drain.
		std::lock_guard<std::mutex> lock(mMutex);
		if (!mQueue.empty())
		{
			out = std::move(mQueue.front());
			mQueue.pop();
			mSize.fetch_sub(1, std::memory_order_release);
			return true;
		}
		return false;
	}

	ExecutionQueueOptions<T>    mOptions;
	std::atomic<bool>           mShutdown;
	std::atomic<size_t>         mSize;
	std::queue<T>               mQueue;
	mutable std::mutex          mMutex;
	std::condition_variable     mNotEmptyCV;
	std::condition_variable     mNotFullCV;
};

} // namespace Network::Concurrency
