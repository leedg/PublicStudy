#pragma once

// English: Key-affinity dispatcher for ordered async execution.
// 한글: 키 친화도 기반 순서 보장 비동기 디스패처.

#include "ExecutionQueue.h"
#include "Utils/Logger.h"
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Network::Concurrency
{
// =============================================================================
// English: KeyedDispatcher
// - Same key always maps to same worker queue.
// - FIFO per worker queue => ordering guarantee per key.
//
// 한글: KeyedDispatcher
// - 동일 key는 항상 동일 worker 큐로 라우팅.
// - worker 큐 FIFO 특성으로 key 단위 순서 보장.
// =============================================================================

class KeyedDispatcher
{
  public:
	struct Options
	{
		size_t mWorkerCount = 0; // English: 0 -> hardware_concurrency fallback
									// 한글: 0 -> hardware_concurrency 사용
		ExecutionQueueOptions<std::function<void()>> mQueueOptions;
		std::string mName = "KeyedDispatcher";
	};

	struct StatsSnapshot
	{
		size_t mSubmitted = 0;
		size_t mRejected = 0;
		size_t mCompleted = 0;
		size_t mFailed = 0;
	};

	KeyedDispatcher()
		: mRunning(false),
		  mSubmitted(0),
		  mRejected(0),
		  mCompleted(0),
		  mFailed(0)
	{
	}

	~KeyedDispatcher()
	{
		Shutdown();
	}

	KeyedDispatcher(const KeyedDispatcher &) = delete;
	KeyedDispatcher &operator=(const KeyedDispatcher &) = delete;

	bool Initialize(const Options &options)
	{
		if (mRunning.load(std::memory_order_acquire))
		{
			Utils::Logger::Warn(options.mName + ": already running");
			return true;
		}

		Options resolvedOptions = options;
		if (resolvedOptions.mWorkerCount == 0)
		{
			resolvedOptions.mWorkerCount = std::thread::hardware_concurrency();
			if (resolvedOptions.mWorkerCount == 0)
			{
				resolvedOptions.mWorkerCount = 4;
			}
		}

		mName = resolvedOptions.mName;
		mWorkers.clear();
		mWorkers.reserve(resolvedOptions.mWorkerCount);
		for (size_t i = 0; i < resolvedOptions.mWorkerCount; ++i)
		{
			mWorkers.push_back(std::make_unique<Worker>(resolvedOptions.mQueueOptions));
		}

		mRunning.store(true, std::memory_order_release);
		for (size_t i = 0; i < mWorkers.size(); ++i)
		{
			mWorkers[i]->mThread =
				std::thread(&KeyedDispatcher::WorkerThreadFunc, this, i);
		}

		Utils::Logger::Info(mName + ": initialized with " +
							std::to_string(mWorkers.size()) + " workers");
		return true;
	}

	void Shutdown()
	{
		bool expected = true;
		if (!mRunning.compare_exchange_strong(
				expected, false, std::memory_order_acq_rel))
		{
			return;
		}

		for (auto &worker : mWorkers)
		{
			worker->mQueue.Shutdown();
		}

		for (auto &worker : mWorkers)
		{
			if (worker->mThread.joinable())
			{
				worker->mThread.join();
			}
		}

		Utils::Logger::Info(mName + ": shutdown complete - submitted=" +
							std::to_string(mSubmitted.load(std::memory_order_relaxed)) +
							", completed=" +
							std::to_string(mCompleted.load(std::memory_order_relaxed)) +
							", failed=" +
							std::to_string(mFailed.load(std::memory_order_relaxed)) +
							", rejected=" +
							std::to_string(mRejected.load(std::memory_order_relaxed)));

		mWorkers.clear();
	}

	bool Dispatch(uint64_t key, std::function<void()> task, int timeoutMs = -1)
	{
		if (!mRunning.load(std::memory_order_acquire) || !task || mWorkers.empty())
		{
			mRejected.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		const size_t workerIndex = KeyToWorkerIndex(key);
		// English: KeyToWorkerIndex() returns (key % mWorkers.size()),
		// so workerIndex is always < mWorkers.size() by definition.
		// This condition is unreachable dead code.
		// 한글: KeyToWorkerIndex()는 (key % mWorkers.size())를 반환하므로
		// workerIndex는 정의상 항상 mWorkers.size()보다 작습니다.
		// 이 조건은 도달 불가능한 dead code입니다.

		bool queued = mWorkers[workerIndex]->mQueue.Push(std::move(task), timeoutMs);
		if (queued)
		{
			mSubmitted.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		mRejected.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	bool IsRunning() const
	{
		return mRunning.load(std::memory_order_acquire);
	}

	size_t GetWorkerCount() const
	{
		return mWorkers.size();
	}

	size_t GetWorkerQueueSize(size_t workerIndex) const
	{
		if (workerIndex >= mWorkers.size())
		{
			return 0;
		}
		return mWorkers[workerIndex]->mQueue.Size();
	}

	StatsSnapshot GetStats() const
	{
		StatsSnapshot snapshot;
		snapshot.mSubmitted = mSubmitted.load(std::memory_order_relaxed);
		snapshot.mRejected = mRejected.load(std::memory_order_relaxed);
		snapshot.mCompleted = mCompleted.load(std::memory_order_relaxed);
		snapshot.mFailed = mFailed.load(std::memory_order_relaxed);
		return snapshot;
	}

  private:
	struct Worker
	{
		explicit Worker(
			const ExecutionQueueOptions<std::function<void()>> &queueOptions)
			: mQueue(queueOptions)
		{
		}

		ExecutionQueue<std::function<void()>> mQueue;
		std::thread mThread;
	};

	size_t KeyToWorkerIndex(uint64_t key) const
	{
		return static_cast<size_t>(key % mWorkers.size());
	}

	void WorkerThreadFunc(size_t workerIndex)
	{
		Worker &worker = *mWorkers[workerIndex];
		std::function<void()> task;

		for (;;)
		{
			if (!worker.mQueue.Pop(task, 100))
			{
				if (!mRunning.load(std::memory_order_acquire) &&
					worker.mQueue.Empty())
				{
					break;
				}
				continue;
			}

			try
			{
				if (task)
				{
					task();
				}
				mCompleted.fetch_add(1, std::memory_order_relaxed);
			}
			catch (const std::exception &e)
			{
				mFailed.fetch_add(1, std::memory_order_relaxed);
				Utils::Logger::Error(mName + ": worker[" +
									 std::to_string(workerIndex) +
									 "] task exception: " + std::string(e.what()));
			}
			catch (...)
			{
				mFailed.fetch_add(1, std::memory_order_relaxed);
				Utils::Logger::Error(mName + ": worker[" +
									 std::to_string(workerIndex) +
									 "] unknown task exception");
			}
		}
	}

	std::string mName;
	std::atomic<bool> mRunning;
	std::vector<std::unique_ptr<Worker>> mWorkers;

	std::atomic<size_t> mSubmitted;
	std::atomic<size_t> mRejected;
	std::atomic<size_t> mCompleted;
	std::atomic<size_t> mFailed;
};

} // namespace Network::Concurrency