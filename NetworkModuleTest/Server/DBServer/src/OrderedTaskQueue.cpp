// OrderedTaskQueue implementation - serverId-based thread affinity

#include "../include/OrderedTaskQueue.h"
#include <exception>
#include <string>

namespace Network::DBServer
{
    using namespace Network::Utils;

    OrderedTaskQueue::OrderedTaskQueue()
        : mWorkerCount(0)
        , mIsRunning(false)
        , mTotalEnqueued(0)
        , mTotalProcessed(0)
        , mTotalFailed(0)
    {
    }

    OrderedTaskQueue::~OrderedTaskQueue()
    {
        if (mIsRunning.load(std::memory_order_acquire))
        {
            Shutdown();
        }
    }

    bool OrderedTaskQueue::Initialize(size_t workerCount)
    {
        if (mIsRunning.load(std::memory_order_acquire))
        {
            Logger::Warn("OrderedTaskQueue already running");
            return true;
        }

        if (workerCount == 0)
        {
            Logger::Error("OrderedTaskQueue: workerCount must be > 0");
            return false;
        }

        Network::Concurrency::KeyedDispatcher::Options options;
        options.mName = "OrderedTaskQueue";
        options.mWorkerCount = workerCount;
        options.mQueueOptions.mBackpressure = Network::Concurrency::BackpressurePolicy::Block;

#ifdef NETWORK_ORDERED_TASKQUEUE_LOCKFREE
        options.mQueueOptions.mBackend = Network::Concurrency::QueueBackend::LockFree;
        options.mQueueOptions.mCapacity = 8192;
        Logger::Info("OrderedTaskQueue: lock-free backend enabled");
#else
        // Default to mutex backend for predictable behavior.
        options.mQueueOptions.mBackend = Network::Concurrency::QueueBackend::Mutex;
        options.mQueueOptions.mCapacity = 8192;
#endif

        if (!mDispatcher.Initialize(options))
        {
            Logger::Error("OrderedTaskQueue: dispatcher initialize failed");
            return false;
        }

        mWorkerCount = workerCount;
        mTotalEnqueued.store(0, std::memory_order_relaxed);
        mTotalProcessed.store(0, std::memory_order_relaxed);
        mTotalFailed.store(0, std::memory_order_relaxed);
        mIsRunning.store(true, std::memory_order_release);

        Logger::Info("OrderedTaskQueue initialized successfully");
        return true;
    }

    void OrderedTaskQueue::Shutdown()
    {
        if (!mIsRunning.load(std::memory_order_acquire))
        {
            return;
        }

        Logger::Info("Shutting down OrderedTaskQueue...");
        mIsRunning.store(false, std::memory_order_release);
        
        // mDispatcher.Shutdown() is blocking and waits for all
        //          enqueued tasks to complete. This ensures accurate statistics
        //          before printing them.
        mDispatcher.Shutdown();

        Logger::Info("OrderedTaskQueue shutdown complete - Enqueued: " +
                     std::to_string(mTotalEnqueued.load(std::memory_order_relaxed)) +
                     ", Processed: " +
                     std::to_string(mTotalProcessed.load(std::memory_order_relaxed)) +
                     ", Failed: " +
                     std::to_string(mTotalFailed.load(std::memory_order_relaxed)));
    }

    void OrderedTaskQueue::EnqueueTask(uint32_t key, std::function<void()> taskFunc)
    {
        if (!mIsRunning.load(std::memory_order_acquire))
        {
            Logger::Error("Cannot enqueue task - OrderedTaskQueue not running");
            return;
        }

        // mTotalProcessed / mTotalFailed are tracked here in the wrapper.
        //          KeyedDispatcher::WorkerThreadFunc also tracks mCompleted/mFailed
        //          independently — use GetStats() for dispatcher-level metrics,
        //          GetTotalProcessedCount() for OrderedTaskQueue-level metrics.
        bool queued = mDispatcher.Dispatch(
            key,
            [this, workerKey = key, task = std::move(taskFunc)]() mutable {
                try
                {
                    if (task)
                    {
                        task();
                    }
                    mTotalProcessed.fetch_add(1, std::memory_order_relaxed);
                }
                catch (const std::exception& e)
                {
                    mTotalFailed.fetch_add(1, std::memory_order_relaxed);
                    Logger::Error("OrderedTaskQueue task exception - key: " +
                                  std::to_string(workerKey) + ", error: " + e.what());
                }
                catch (...)
                {
                    mTotalFailed.fetch_add(1, std::memory_order_relaxed);
                    Logger::Error("OrderedTaskQueue unknown task exception - key: " +
                                  std::to_string(workerKey));
                }
            });

        if (queued)
        {
            mTotalEnqueued.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // Count silently dropped tasks so shutdown stats reflect true failures.
        mTotalFailed.fetch_add(1, std::memory_order_relaxed);
        Logger::Warn("OrderedTaskQueue enqueue rejected - key: " +
                     std::to_string(key));
    }

    size_t OrderedTaskQueue::GetWorkerQueueSize(size_t workerIndex) const
    {
        return mDispatcher.GetWorkerQueueSize(workerIndex);
    }

} // namespace Network::DBServer
