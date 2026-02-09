#pragma once

// English: OrderedTaskQueue - guarantees per-key ordering with serverId-based thread affinity
// 한글: OrderedTaskQueue - serverId 기반 스레드 친화도로 키별 순서 보장
//
// English: Design rationale:
//   Multiple worker threads handle tasks concurrently, but tasks for the SAME serverId
//   are always dispatched to the SAME worker thread (hash-based affinity).
//   This ensures:
//     1. Per-server ordering: tasks for server A execute in FIFO order
//     2. Concurrency between servers: server A and server B run in parallel
//     3. No lock contention: each worker has its own independent queue
//
// 한글: 설계 원리:
//   여러 워커 스레드가 동시에 작업을 처리하되, 같은 serverId의 작업은
//   항상 같은 워커 스레드에 배정됩니다 (해시 기반 친화도).
//   이를 통해:
//     1. 서버별 순서 보장: 서버 A의 작업은 FIFO 순서로 실행
//     2. 서버 간 동시성: 서버 A와 서버 B는 병렬 실행
//     3. 락 경합 없음: 각 워커가 독립적인 큐를 보유

#include "Utils/NetworkUtils.h"
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>

namespace Network::DBServer
{
    // =============================================================================
    // English: Task item for ordered queue
    // 한글: 순서 보장 큐용 작업 항목
    // =============================================================================

    struct OrderedTask
    {
        uint32_t key;                       // English: Routing key (serverId)
                                            // 한글: 라우팅 키 (serverId)
        std::function<void()> taskFunc;     // English: Task functor
                                            // 한글: 작업 펑터

        OrderedTask() : key(0) {}
        OrderedTask(uint32_t k, std::function<void()> func)
            : key(k), taskFunc(std::move(func)) {}
    };

    // =============================================================================
    // English: Per-worker queue (each worker thread owns one)
    // 한글: 워커별 큐 (각 워커 스레드가 하나를 소유)
    // =============================================================================

    struct WorkerQueue
    {
        std::queue<OrderedTask>         taskQueue;
        std::mutex                      queueMutex;
        std::condition_variable         queueCV;
        std::atomic<size_t>             queueSize{ 0 };
    };

    // =============================================================================
    // English: OrderedTaskQueue - serverId-based thread affinity task queue
    // 한글: OrderedTaskQueue - serverId 기반 스레드 친화도 작업 큐
    // =============================================================================

    class OrderedTaskQueue
    {
    public:
        OrderedTaskQueue();
        ~OrderedTaskQueue();

        // English: Initialize with specified number of worker threads
        // 한글: 지정된 수의 워커 스레드로 초기화
        // @param workerCount  Number of worker threads (default: 4)
        //                     Each worker owns an independent queue.
        //                     serverId is hashed to select the worker.
        bool Initialize(size_t workerCount = 4);

        // English: Shutdown all workers gracefully (drain remaining tasks)
        // 한글: 모든 워커를 정상 종료 (남은 작업 소진)
        void Shutdown();

        // English: Enqueue a task routed by key (serverId)
        //          Tasks with the same key are guaranteed to execute in order.
        // 한글: 키(serverId)로 라우팅되는 작업 추가
        //       같은 키를 가진 작업은 순서대로 실행됨을 보장
        void EnqueueTask(uint32_t key, std::function<void()> taskFunc);

        // English: Check if queue is running
        // 한글: 큐가 실행 중인지 확인
        bool IsRunning() const { return mIsRunning.load(); }

        // English: Get total enqueued count
        // 한글: 총 큐잉된 작업 수 조회
        size_t GetTotalEnqueuedCount() const { return mTotalEnqueued.load(); }

        // English: Get total processed count
        // 한글: 총 처리된 작업 수 조회
        size_t GetTotalProcessedCount() const { return mTotalProcessed.load(); }

        // English: Get worker count
        // 한글: 워커 수 조회
        size_t GetWorkerCount() const { return mWorkerCount; }

        // English: Get queue size for a specific worker
        // 한글: 특정 워커의 큐 크기 조회
        size_t GetWorkerQueueSize(size_t workerIndex) const;

    private:
        // English: Worker thread function (each processes its own queue)
        // 한글: 워커 스레드 함수 (각각 자신의 큐를 처리)
        void WorkerThreadFunc(size_t workerIndex);

        // English: Hash key to worker index (determines thread affinity)
        // 한글: 키를 워커 인덱스로 해시 (스레드 친화도 결정)
        size_t KeyToWorkerIndex(uint32_t key) const;

    private:
        size_t                          mWorkerCount;
        std::atomic<bool>               mIsRunning;

        // English: Per-worker queues (independent, no shared contention)
        // 한글: 워커별 큐 (독립적, 공유 경합 없음)
        std::vector<std::unique_ptr<WorkerQueue>>   mWorkerQueues;

        // English: Worker threads
        // 한글: 워커 스레드
        std::vector<std::thread>        mWorkerThreads;

        // English: Global statistics (atomic, lock-free)
        // 한글: 전역 통계 (atomic, lock-free)
        std::atomic<size_t>             mTotalEnqueued;
        std::atomic<size_t>             mTotalProcessed;
        std::atomic<size_t>             mTotalFailed;
    };

} // namespace Network::DBServer
