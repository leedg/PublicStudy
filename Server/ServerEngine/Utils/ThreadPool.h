#pragma once

// English: Thread pool implementation
// 한글: 스레드 풀 구현

#include "Logger.h"
#include "SafeQueue.h"
#include <vector>
#include <thread>
#include <functional>
#include <future>
#include <atomic>
#include <memory>
#include <condition_variable>
#include <mutex>

namespace Network::Utils
{
// =============================================================================
// English: ThreadPool - manages a pool of worker threads for async tasks
// 한글: ThreadPool - 비동기 작업을 위한 워커 스레드 풀 관리
// =============================================================================

class ThreadPool
{
public:
	// English: Constructor - creates worker threads
	// 한글: 생성자 - 워커 스레드 생성
	// @param numThreads - Number of threads (0 = hardware concurrency)
	// @param maxQueueDepth - Max task queue depth (0 = unlimited)
	ThreadPool(size_t numThreads = std::thread::hardware_concurrency(),
			   size_t maxQueueDepth = 0)
		: mStop(false), mActiveTasks(0), mTasks(maxQueueDepth)
	{
		if (numThreads == 0)
			numThreads = 4;

		for (size_t i = 0; i < numThreads; ++i)
		{
			mWorkers.emplace_back(&ThreadPool::WorkerThread, this);
		}
	}

	// English: Destructor - stops all threads and waits for completion
	// 한글: 소멸자 - 모든 스레드 중지 및 완료 대기
	~ThreadPool()
	{
		mStop = true;
		mTasks.Shutdown();

		for (auto &worker : mWorkers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}
	}

	// English: Submit a task to the thread pool - returns false if queue is full
	// 한글: 스레드 풀에 작업 제출 - 큐가 가득 찬 경우 false 반환
	// @param f - Function to execute
	// @param args - Arguments for the function
	// @return true if task was queued, false if queue was full (task dropped)
	template <typename F, typename... Args>
	bool Submit(F &&f, Args &&...args)
	{
		auto task = std::make_shared<std::packaged_task<void()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...));

		if (!mTasks.Push([task]() { (*task)(); }))
		{
			Logger::Warn("[ThreadPool] Task queue full - task dropped");
			return false;
		}

		return true;
	}

	// English: Wait for all tasks to complete
	// 한글: 모든 작업 완료 대기
	void WaitForAll()
	{
		std::unique_lock<std::mutex> lock(mWaitMutex);
		mWaitCV.wait(lock, [this] {
			return mActiveTasks == 0 && mTasks.Empty();
		});
	}

	// English: Get number of worker threads
	// 한글: 워커 스레드 수 가져오기
	size_t GetThreadCount() const { return mWorkers.size(); }

	// English: Get number of active tasks
	// 한글: 활성 작업 수 가져오기
	size_t GetActiveTaskCount() const { return mActiveTasks.load(); }

private:
	std::vector<std::thread> mWorkers;
	SafeQueue<std::function<void()>> mTasks;
	std::atomic<bool> mStop;
	std::atomic<size_t> mActiveTasks;
	std::mutex mWaitMutex;
	std::condition_variable mWaitCV;

	// English: Worker thread function
	// 한글: 워커 스레드 함수
	void WorkerThread()
	{
		while (!mStop)
		{
			std::function<void()> task;
			if (mTasks.Pop(task, 100))
			{
				++mActiveTasks;
				try
				{
					task();
				}
				catch (const std::exception &e)
				{
					// English: Log exception from worker thread task
					// 한글: 워커 스레드 작업에서 발생한 예외 로깅
					Logger::Error("[ThreadPool] Task threw exception: " +
								  std::string(e.what()));
				}
				catch (...)
				{
					// English: Catch unknown exception types
					// 한글: 알 수 없는 예외 타입 처리
					Logger::Error("[ThreadPool] Task threw unknown exception");
				}
				--mActiveTasks;
			// English: Notify WaitForAll() when task completes
			// 한글: 작업 완료 시 WaitForAll() 알림
			mWaitCV.notify_one();
			}
		}
	}
};

} // namespace Network::Utils
