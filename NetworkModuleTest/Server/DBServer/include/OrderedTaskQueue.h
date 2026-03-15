#pragma once

// OrderedTaskQueue - per-key ordered execution (serverId affinity).

#include "Utils/NetworkUtils.h"
#include "Concurrency/ExecutionQueue.h"
#include "Concurrency/KeyedDispatcher.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace Network::DBServer
{
    // =============================================================================
    // OrderedTaskQueue (public facade)
    // =============================================================================

    class OrderedTaskQueue
    {
    public:
        OrderedTaskQueue();
        ~OrderedTaskQueue();

        // Initialize with specified number of worker threads
        bool Initialize(size_t workerCount = Utils::DEFAULT_DB_WORKER_COUNT);

        // Shutdown all workers gracefully (drain remaining tasks)
        void Shutdown();

        // Enqueue a task routed by key (serverId)
        //          Tasks with the same key are guaranteed to execute in order.
        void EnqueueTask(uint32_t key, std::function<void()> taskFunc);

        // Check if queue is running
        bool IsRunning() const { return mIsRunning.load(std::memory_order_acquire); }

        // Get total enqueued count
        size_t GetTotalEnqueuedCount() const { return mTotalEnqueued.load(std::memory_order_relaxed); }

        // Get total processed count
        size_t GetTotalProcessedCount() const { return mTotalProcessed.load(std::memory_order_relaxed); }

        // Get worker count
        size_t GetWorkerCount() const { return mWorkerCount; }

        // Get queue size for a specific worker
        size_t GetWorkerQueueSize(size_t workerIndex) const;

    private:
        size_t                          mWorkerCount;
        std::atomic<bool>               mIsRunning;
        Network::Concurrency::KeyedDispatcher mDispatcher;

        // Global statistics (atomic, lock-free)
        std::atomic<size_t>             mTotalEnqueued;
        std::atomic<size_t>             mTotalProcessed;
        std::atomic<size_t>             mTotalFailed;
    };

} // namespace Network::DBServer
