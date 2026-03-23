#pragma once

// 일반 목적 스레드 풀: 비동기 태스크 실행을 위한 워커 스레드 풀.

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
// ThreadPool
//
// 고정 크기 워커 스레드 풀. Submit()으로 태스크를 제출하고,
// WaitForAll()로 모든 태스크가 완료될 때까지 블로킹 대기할 수 있다.
// =============================================================================

class ThreadPool
{
public:
	// 워커 스레드를 생성한다.
	// @param numThreads    스레드 수 (0 이면 hardware_concurrency 사용; 그것도 0이면 4로 폴백)
	// @param maxQueueDepth 태스크 큐 최대 깊이 (0 = 무제한)
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

	// 모든 워커 스레드를 중지하고 완료를 대기한다.
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

	// 태스크를 풀에 제출한다.
	// 큐가 가득 찬 경우(maxQueueDepth > 0) 태스크를 버리고 false를 반환한다.
	// @param f    실행할 함수
	// @param args 함수에 전달할 인자
	// @return true = 큐에 등록됨, false = 큐 가득 참 (태스크 유실)
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

	// 모든 활성 태스크가 완료될 때까지 호출자 스레드를 블로킹한다.
	//
	// mActiveTasks의 증가(태스크 시작 직전)와 감소(태스크 완료 직후) 사이에
	// ±1 오차 구간이 존재한다. 조건식에 mTasks.Empty()를 함께 확인함으로써
	// 큐에 남은 미처리 태스크까지 포함해 완전한 drain을 보장한다.
	// 람다 조건식이 spurious wakeup을 방어한다.
	void WaitForAll()
	{
		std::unique_lock<std::mutex> lock(mWaitMutex);
		mWaitCV.wait(lock, [this] {
			return mActiveTasks == 0 && mTasks.Empty();
		});
	}

	size_t GetThreadCount() const { return mWorkers.size(); }
	size_t GetActiveTaskCount() const { return mActiveTasks.load(); }

private:
	std::vector<std::thread> mWorkers;
	SafeQueue<std::function<void()>> mTasks;
	std::atomic<bool> mStop;
	std::atomic<size_t> mActiveTasks;
	std::mutex mWaitMutex;
	std::condition_variable mWaitCV;

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
					Logger::Error("[ThreadPool] Task threw exception: " +
								  std::string(e.what()));
				}
				catch (...)
				{
					Logger::Error("[ThreadPool] Task threw unknown exception");
				}
				--mActiveTasks;
				// 태스크 완료마다 WaitForAll() 대기자에게 알린다.
				mWaitCV.notify_one();
			}
		}
	}
};

} // namespace Network::Utils
