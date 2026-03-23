#pragma once

// 블로킹 Push/Pop을 지원하는 스레드 안전 큐.

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace Network::Utils
{
// =============================================================================
// SafeQueue<T>
//
// Push()/Emplace()는 큐가 가득 찼을 때(maxSize > 0) 즉시 false를 반환한다.
// Pop()은 타임아웃이 만료되거나 Shutdown() 전까지 블로킹 대기한다.
// =============================================================================

template <typename T>
class SafeQueue
{
public:
	// 큐의 최대 크기를 설정한다. 0 = 무제한.
	explicit SafeQueue(size_t maxSize = 0) : mMaxSize(maxSize) {}

	// 큐에 항목을 추가한다 (복사). 큐가 가득 찬 경우 false를 반환한다.
	bool Push(const T &item)
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mMaxSize > 0 && mQueue.size() >= mMaxSize)
				return false;
			mQueue.push(item);
			// 락을 보유한 채 notify한다. 락 해제 전에 대기자가 새 항목을 인식하므로
			// wait_for의 타임아웃이 불필요하게 만료되는 상황을 방지한다.
			mCondition.notify_one();
		}
		return true;
	}

	// 큐에 항목을 추가한다 (이동). 큐가 가득 찬 경우 false를 반환한다.
	bool Push(T &&item)
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mMaxSize > 0 && mQueue.size() >= mMaxSize)
				return false;
			mQueue.push(std::move(item));
			// 락 보유 중 notify (Push(const T&) 주석 참조).
			mCondition.notify_one();
		}
		return true;
	}

	// 큐에 항목을 직접 생성한다. 큐가 가득 찬 경우 false를 반환한다.
	template<typename... Args>
	bool Emplace(Args&&... args)
	{
		{
			std::lock_guard<std::mutex> lock(mMutex);
			if (mMaxSize > 0 && mQueue.size() >= mMaxSize)
				return false;
			mQueue.emplace(std::forward<Args>(args)...);
			// 락 보유 중 notify (Push(const T&) 주석 참조).
			mCondition.notify_one();
		}
		return true;
	}

	// 큐에서 항목을 꺼낸다 (블로킹).
	// 람다 조건식이 spurious wakeup을 방어한다.
	// OS는 notify 없이도 wait를 깨울 수 있으므로, 람다가 false를 반환하면
	// wait/wait_for가 자동으로 재대기한다.
	// @param item      꺼낸 항목을 저장할 참조
	// @param timeoutMs 타임아웃(ms). -1 = 무한 대기
	// @return true = 항목 획득, false = 타임아웃 또는 shutdown
	bool Pop(T &item, int timeoutMs = -1)
	{
		std::unique_lock<std::mutex> lock(mMutex);

		if (timeoutMs < 0)
		{
			mCondition.wait(lock, [this] { return !mQueue.empty() || mShutdown; });
		}
		else
		{
			if (!mCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
									 [this] { return !mQueue.empty() || mShutdown; }))
			{
				return false; // 타임아웃
			}
		}

		if (mQueue.empty())
			return false;

		item = std::move(mQueue.front());
		mQueue.pop();
		return true;
	}

	bool Empty() const
	{
		std::lock_guard<std::mutex> lock(mMutex);
		return mQueue.empty();
	}

	size_t Size() const
	{
		std::lock_guard<std::mutex> lock(mMutex);
		return mQueue.size();
	}

	// 큐를 종료하고 대기 중인 모든 스레드를 깨운다.
	// 이후 Pop()은 큐가 비어있으면 false를 반환한다.
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
