#pragma once

// Thread-safe queue implementation

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace Network::Utils
{
// =============================================================================
// SafeQueue - thread-safe queue with blocking operations
// =============================================================================

template <typename T>
class SafeQueue
{
public:
	// Constructor - optionally limit queue capacity (0 = unlimited)
	explicit SafeQueue(size_t maxSize = 0) : mMaxSize(maxSize) {}

	// Push an item to the queue (copy) - returns false if queue is full
	bool Push(const T &item)
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mMaxSize > 0 && mQueue.size() >= mMaxSize)
				return false;
			mQueue.push(item);
			// Notify while holding the lock so the waiter sees the new item
			//          before the mutex is released (avoids spurious wait_for timeouts).
			mCondition.notify_one();
		}
		return true;
	}

	// Push an item to the queue (move) - returns false if queue is full
	bool Push(T &&item)
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mMaxSize > 0 && mQueue.size() >= mMaxSize)
				return false;
			mQueue.push(std::move(item));
			// Notify while holding the lock (see Push(const T&) comment)
			mCondition.notify_one();
		}
		return true;
	}

	// Emplace an item directly in the queue - returns false if queue is full
	template<typename... Args>
	bool Emplace(Args&&... args)
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mMaxSize > 0 && mQueue.size() >= mMaxSize)
				return false;
			mQueue.emplace(std::forward<Args>(args)...);
			// Notify while holding the lock (see Push(const T&) comment)
			mCondition.notify_one();
		}
		return true;
	}

	// Pop an item from the queue (blocking)
	// @param item - Reference to store the popped item
	// @param timeoutMs - Timeout in milliseconds (-1 = wait forever)
	// @return true if item was popped, false if timeout or shutdown
	bool Pop(T &item, int timeoutMs = -1)
	{
		std::unique_lock<std::mutex> lock(mMutex);

		if (timeoutMs < 0)
		{
			// Wait indefinitely until item is available or shutdown
			mCondition.wait(lock, [this] { return !mQueue.empty() || mShutdown; });
		}
		else
		{
			// Wait with timeout
			if (!mCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
									 [this] { return !mQueue.empty() || mShutdown; }))
			{
				return false; // Timeout
			}
		}

		if (mQueue.empty())
			return false;

		item = std::move(mQueue.front());
		mQueue.pop();
		return true;
	}

	// Check if queue is empty
	bool Empty() const
	{
		std::lock_guard<std::mutex> lock(mMutex);
		return mQueue.empty();
	}

	// Get queue size
	size_t Size() const
	{
		std::lock_guard<std::mutex> lock(mMutex);
		return mQueue.size();
	}

	// Shutdown the queue and wake all waiting threads
	void Shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			mShutdown = true;
		}
		mCondition.notify_all();
	}

private:
	std::queue<T> mQueue;
	mutable std::mutex mMutex;
	std::condition_variable mCondition;
	bool mShutdown = false;
	size_t mMaxSize = 0;
};

} // namespace Network::Utils
