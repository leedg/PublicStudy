#pragma once

// Key-affinity dispatcher for ordered async execution.

#include "ExecutionQueue.h"
#include "Utils/Logger.h"
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Network::Concurrency
{
// =============================================================================
// KeyedDispatcher
// - Same key always maps to same worker queue.
// - FIFO per worker queue => ordering guarantee per key.
//
// =============================================================================

class KeyedDispatcher
{
  public:
	struct Options
	{
		size_t mWorkerCount = 0; // 0 -> hardware_concurrency fallback
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

		{
			// Exclusive lock — prevents Dispatch() from accessing mWorkers while we clear.
			std::unique_lock<std::shared_mutex> exclusiveLock(mWorkersMutex);
			mWorkers.clear();
		}
	}

	bool Dispatch(uint64_t key, std::function<void()> task, int timeoutMs = -1)
	{
		// Shared lock prevents TOCTOU race with Shutdown() calling mWorkers.clear().
		//          Checked under the lock so mWorkers cannot be cleared between check and access.
		std::shared_lock<std::shared_mutex> sharedLock(mWorkersMutex);

		if (!mRunning.load(std::memory_order_acquire) || !task || mWorkers.empty())
		{
			mRejected.fetch_add(1, std::memory_order_relaxed);
			return false;
		}

		const size_t workerIndex = KeyToWorkerIndex(key);

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
		std::shared_lock<std::shared_mutex> sharedLock(mWorkersMutex);
		return mWorkers.size();
	}

	size_t GetWorkerQueueSize(size_t workerIndex) const
	{
		std::shared_lock<std::shared_mutex> sharedLock(mWorkersMutex);
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
		// Raw reference is safe: mWorkers is only cleared in Shutdown() under
		//          an exclusive lock, AFTER all worker threads have been joined.
		//          Thread join happens before clear(), so this reference is always valid
		//          for the lifetime of this function.
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
	mutable std::shared_mutex mWorkersMutex;  // Protects mWorkers during concurrent Dispatch/Shutdown

	std::atomic<size_t> mSubmitted;
	std::atomic<size_t> mRejected;
	std::atomic<size_t> mCompleted;
	std::atomic<size_t> mFailed;
};

} // namespace Network::Concurrency
