#pragma once

// English: Thread pool implementation
// 한글: 스레드 풀 구현

#include "SafeQueue.h"
#include <vector>
#include <thread>
#include <functional>
#include <future>
#include <atomic>
#include <memory>

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
	// @param numThreads - Number of threads (0 = hardware concurrency, default = 4)
	ThreadPool(size_t numThreads = std::thread::hardware_concurrency())
		: mStop(false), mActiveTasks(0)
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

	// English: Submit a task to the thread pool
	// 한글: 스레드 풀에 작업 제출
	// @param f - Function to execute
	// @param args - Arguments for the function
	// @return Future for the result
	template <typename F, typename... Args>
	auto Submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>
	{
		using ReturnType = decltype(f(args...));

		auto task = std::make_shared<std::packaged_task<ReturnType()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...));

		std::future<ReturnType> result = task->get_future();

		mTasks.Push([task]() { (*task)(); });

		return result;
	}

	// English: Wait for all tasks to complete
	// 한글: 모든 작업 완료 대기
	void WaitForAll()
	{
		while (mActiveTasks > 0 || !mTasks.Empty())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
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
					// English: Swallow exception in worker thread
					// 한글: 워커 스레드에서 예외 무시
					(void)e;
				}
				--mActiveTasks;
			}
		}
	}
};

} // namespace Network::Utils
