#pragma once

// English: Thread-safe queue implementation
// 한글: 스레드 안전 큐 구현

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace Network::Utils
{
// =============================================================================
// English: SafeQueue - thread-safe queue with blocking operations
// 한글: SafeQueue - 블로킹 작업이 가능한 스레드 안전 큐
// =============================================================================

template <typename T>
class SafeQueue
{
public:
	// English: Constructor - optionally limit queue capacity (0 = unlimited)
	// 한글: 생성자 - 선택적으로 큐 최대 크기 제한 (0 = 무제한)
	explicit SafeQueue(size_t maxSize = 0) : mMaxSize(maxSize) {}

	// English: Push an item to the queue (copy) - returns false if queue is full
	// 한글: 큐에 항목 추가 (복사) - 큐가 가득 찬 경우 false 반환
	bool Push(const T &item)
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mMaxSize > 0 && mQueue.size() >= mMaxSize)
				return false;
			mQueue.push(item);
		}
		// English: Notify outside lock to avoid waking thread while holding lock
		// 한글: 락을 잡은 채로 스레드를 깨우는 것을 방지하기 위해 락 밖에서 알림
		mCondition.notify_one();
		return true;
	}

	// English: Push an item to the queue (move) - returns false if queue is full
	// 한글: 큐에 항목 추가 (이동) - 큐가 가득 찬 경우 false 반환
	bool Push(T &&item)
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mMaxSize > 0 && mQueue.size() >= mMaxSize)
				return false;
			mQueue.push(std::move(item));
		}
		mCondition.notify_one();
		return true;
	}

	// English: Emplace an item directly in the queue - returns false if queue is full
	// 한글: 큐에 항목 직접 생성 - 큐가 가득 찬 경우 false 반환
	template<typename... Args>
	bool Emplace(Args&&... args)
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mMaxSize > 0 && mQueue.size() >= mMaxSize)
				return false;
			mQueue.emplace(std::forward<Args>(args)...);
		}
		mCondition.notify_one();
		return true;
	}

	// English: Pop an item from the queue (blocking)
	// 한글: 큐에서 항목 제거 (블로킹)
	// @param item - Reference to store the popped item
	// @param timeoutMs - Timeout in milliseconds (-1 = wait forever)
	// @return true if item was popped, false if timeout or shutdown
	bool Pop(T &item, int timeoutMs = -1)
	{
		std::unique_lock<std::mutex> lock(mMutex);

		if (timeoutMs < 0)
		{
			// English: Wait indefinitely until item is available or shutdown
			// 한글: 항목이 사용 가능하거나 종료할 때까지 무한 대기
			mCondition.wait(lock, [this] { return !mQueue.empty() || mShutdown; });
		}
		else
		{
			// English: Wait with timeout
			// 한글: 타임아웃과 함께 대기
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

	// English: Check if queue is empty
	// 한글: 큐가 비어있는지 확인
	bool Empty() const
	{
		std::lock_guard<std::mutex> lock(mMutex);
		return mQueue.empty();
	}

	// English: Get queue size
	// 한글: 큐 크기 가져오기
	size_t Size() const
	{
		std::lock_guard<std::mutex> lock(mMutex);
		return mQueue.size();
	}

	// English: Shutdown the queue and wake all waiting threads
	// 한글: 큐를 종료하고 대기 중인 모든 스레드 깨우기
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
