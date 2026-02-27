#pragma once

// English: OrderedTaskQueue - per-key ordered execution (serverId affinity).
// 한글: OrderedTaskQueue - key(serverId) 단위 순서 보장 실행 큐.

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
    // English: OrderedTaskQueue (public facade)
    // 한글: OrderedTaskQueue (외부 호환용 facade)
    // =============================================================================

    class OrderedTaskQueue
    {
    public:
        OrderedTaskQueue();
        ~OrderedTaskQueue();

        // English: Initialize with specified number of worker threads
        // 한글: 지정한 수의 워커 스레드로 초기화
        bool Initialize(size_t workerCount = 4);

        // English: Shutdown all workers gracefully (drain remaining tasks)
        // 한글: 모든 워커 정상 종료 (남은 작업 drain)
        void Shutdown();

        // English: Enqueue a task routed by key (serverId)
        //          Tasks with the same key are guaranteed to execute in order.
        // 한글: key(serverId) 기반 라우팅 enqueue (같은 key 순서 보장)
        void EnqueueTask(uint32_t key, std::function<void()> taskFunc);

        // English: Check if queue is running
        // 한글: 큐 실행 상태 조회
        bool IsRunning() const { return mIsRunning.load(std::memory_order_acquire); }

        // English: Get total enqueued count
        // 한글: 총 enqueue 수 조회
        size_t GetTotalEnqueuedCount() const { return mTotalEnqueued.load(std::memory_order_relaxed); }

        // English: Get total processed count
        // 한글: 총 처리 수 조회
        size_t GetTotalProcessedCount() const { return mTotalProcessed.load(std::memory_order_relaxed); }

        // English: Get worker count
        // 한글: 워커 수 조회
        size_t GetWorkerCount() const { return mWorkerCount; }

        // English: Get queue size for a specific worker
        // 한글: 특정 워커의 큐 길이 조회
        size_t GetWorkerQueueSize(size_t workerIndex) const;

    private:
        size_t                          mWorkerCount;
        std::atomic<bool>               mIsRunning;
        Network::Concurrency::KeyedDispatcher mDispatcher;

        // English: Global statistics (atomic, lock-free)
        // 한글: 전역 통계 (atomic, lock-free)
        std::atomic<size_t>             mTotalEnqueued;
        std::atomic<size_t>             mTotalProcessed;
        std::atomic<size_t>             mTotalFailed;
    };

} // namespace Network::DBServer
