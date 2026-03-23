#pragma once

// OrderedTaskQueue - key(serverId) 단위 순서 보장 실행 큐.
//   KeyedDispatcher를 래핑하여 serverId % workerCount 해시 친화도를 제공한다.
//   동일 serverId의 작업은 항상 같은 워커 스레드에 배정되므로
//   per-serverId FIFO 순서가 보장된다. (동일 서버의 레이턴시 기록이 뒤섞이지 않음)

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
    // OrderedTaskQueue — KeyedDispatcher를 래핑한 외부 공개 facade.
    //   KeyedDispatcher는 플랫폼 별 고성능 I/O 큐로 직접 노출하기엔 인터페이스가 복잡하므로
    //   이 래퍼가 간결한 EnqueueTask(key, func) API를 제공한다.
    // =============================================================================

    class OrderedTaskQueue
    {
    public:
        OrderedTaskQueue();
        ~OrderedTaskQueue();

        // 지정한 수의 워커 스레드로 초기화.
        //   DEFAULT_DB_WORKER_COUNT(4)는 DB I/O 병렬성과 serverId 해시 분산의 균형점이다.
        bool Initialize(size_t workerCount = Utils::DEFAULT_DB_WORKER_COUNT);

        // 모든 워커 정상 종료 (남은 작업 drain 후 중지)
        void Shutdown();

        // key(serverId) 기반 라우팅 enqueue.
        //   같은 key는 항상 같은 워커로 배정되므로 per-key 순서가 보장된다.
        void EnqueueTask(uint32_t key, std::function<void()> taskFunc);

        // 큐 실행 상태 조회
        bool IsRunning() const { return mIsRunning.load(std::memory_order_acquire); }

        // 통계 조회 (atomic, lock-free)
        size_t GetTotalEnqueuedCount() const { return mTotalEnqueued.load(std::memory_order_relaxed); }
        size_t GetTotalProcessedCount() const { return mTotalProcessed.load(std::memory_order_relaxed); }

        // 워커 수 및 특정 워커의 현재 큐 길이 조회
        size_t GetWorkerCount() const { return mWorkerCount; }
        size_t GetWorkerQueueSize(size_t workerIndex) const;

    private:
        size_t                          mWorkerCount;
        std::atomic<bool>               mIsRunning;
        Network::Concurrency::KeyedDispatcher mDispatcher;

        // 전역 통계 (atomic, lock-free)
        std::atomic<size_t>             mTotalEnqueued;
        std::atomic<size_t>             mTotalProcessed;
        std::atomic<size_t>             mTotalFailed;
    };

} // namespace Network::DBServer
