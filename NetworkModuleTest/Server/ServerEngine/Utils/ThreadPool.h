#pragma once

// Thread pool implementation

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
// ThreadPool - manages a pool of worker threads for async tasks
// =============================================================================

class ThreadPool
{
public:
	// Constructor - creates worker threads
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

	// Destructor - stops all threads and waits for completion
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

	// Submit a task to the thread pool - returns false if queue is full
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

	// Wait for all tasks to complete
	// Note: There may be a ±1 tolerance in mActiveTasks during task transitions.
	// Between the moment a worker increments mActiveTasks and processes a task,
	// or between completing the task and decrementing mActiveTasks, a small
	// discrepancy may occur. This is acceptable for synchronization purposes.
	void WaitForAll()
	{
		std::unique_lock<std::mutex> lock(mWaitMutex);
		mWaitCV.wait(lock, [this] {
			return mActiveTasks == 0 && mTasks.Empty();
		});
	}

	// Get number of worker threads
	size_t GetThreadCount() const { return mWorkers.size(); }

	// Get number of active tasks
	size_t GetActiveTaskCount() const { return mActiveTasks.load(); }

private:
	std::vector<std::thread> mWorkers;
	SafeQueue<std::function<void()>> mTasks;
	std::atomic<bool> mStop;
	std::atomic<size_t> mActiveTasks;
	std::mutex mWaitMutex;
	std::condition_variable mWaitCV;

	// Worker thread function
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
					// Log exception from worker thread task
					Logger::Error("[ThreadPool] Task threw exception: " +
								  std::string(e.what()));
				}
				catch (...)
				{
					// Catch unknown exception types
					Logger::Error("[ThreadPool] Task threw unknown exception");
				}
				--mActiveTasks;
				// Notify WaitForAll() when task completes
				mWaitCV.notify_one();
			}
		}
	}
};

} // namespace Network::Utils
