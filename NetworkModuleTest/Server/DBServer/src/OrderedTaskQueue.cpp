// English: OrderedTaskQueue implementation - serverId-based thread affinity
// 한글: OrderedTaskQueue 구현 - serverId 기반 스레드 친화도

#include "../include/OrderedTaskQueue.h"

namespace Network::DBServer
{
    using namespace Network::Utils;

    // =============================================================================
    // English: OrderedTaskQueue implementation
    // 한글: OrderedTaskQueue 구현
    // =============================================================================

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
        if (mIsRunning.load())
        {
            Shutdown();
        }
    }

    bool OrderedTaskQueue::Initialize(size_t workerCount)
    {
        if (mIsRunning.load())
        {
            Logger::Warn("OrderedTaskQueue already running");
            return true;
        }

        if (workerCount == 0)
        {
            Logger::Error("OrderedTaskQueue: workerCount must be > 0");
            return false;
        }

        mWorkerCount = workerCount;
        mIsRunning.store(true);

        Logger::Info("Initializing OrderedTaskQueue with " + std::to_string(workerCount) +
                     " worker threads (serverId-based affinity)");

        // English: Create per-worker queues
        // 한글: 워커별 큐 생성
        mWorkerQueues.reserve(workerCount);
        for (size_t i = 0; i < workerCount; ++i)
        {
            mWorkerQueues.push_back(std::make_unique<WorkerQueue>());
        }

        // English: Start worker threads
        // 한글: 워커 스레드 시작
        mWorkerThreads.reserve(workerCount);
        for (size_t i = 0; i < workerCount; ++i)
        {
            mWorkerThreads.emplace_back(&OrderedTaskQueue::WorkerThreadFunc, this, i);
        }

        Logger::Info("OrderedTaskQueue initialized successfully");
        return true;
    }

    void OrderedTaskQueue::Shutdown()
    {
        if (!mIsRunning.load())
        {
            return;
        }

        Logger::Info("Shutting down OrderedTaskQueue...");

        // English: Signal all workers to stop
        // 한글: 모든 워커에 중지 신호 전송
        mIsRunning.store(false);

        // English: Wake up all workers
        // 한글: 모든 워커 깨우기
        for (auto& wq : mWorkerQueues)
        {
            wq->queueCV.notify_all();
        }

        // English: Wait for all workers to finish
        // 한글: 모든 워커가 종료될 때까지 대기
        for (auto& thread : mWorkerThreads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        mWorkerThreads.clear();

        // English: Log remaining tasks per worker
        // 한글: 워커별 남은 작업 수 로그
        for (size_t i = 0; i < mWorkerQueues.size(); ++i)
        {
            size_t remaining = mWorkerQueues[i]->queueSize.load();
            if (remaining > 0)
            {
                Logger::Warn("OrderedTaskQueue worker[" + std::to_string(i) +
                             "] shutdown with " + std::to_string(remaining) + " tasks remaining");
            }
        }
        mWorkerQueues.clear();

        Logger::Info("OrderedTaskQueue shutdown complete - Enqueued: " +
                     std::to_string(mTotalEnqueued.load()) +
                     ", Processed: " + std::to_string(mTotalProcessed.load()) +
                     ", Failed: " + std::to_string(mTotalFailed.load()));
    }

    void OrderedTaskQueue::EnqueueTask(uint32_t key, std::function<void()> taskFunc)
    {
        if (!mIsRunning.load())
        {
            Logger::Error("Cannot enqueue task - OrderedTaskQueue not running");
            return;
        }

        // English: Hash serverId to determine target worker (thread affinity)
        // 한글: serverId를 해시하여 대상 워커 결정 (스레드 친화도)
        size_t workerIdx = KeyToWorkerIndex(key);
        auto& wq = mWorkerQueues[workerIdx];

        {
            std::lock_guard<std::mutex> lock(wq->queueMutex);
            wq->taskQueue.emplace(key, std::move(taskFunc));
            wq->queueSize.fetch_add(1, std::memory_order_relaxed);
        }

        // English: Increment global counter
        // 한글: 전역 카운터 증가
        mTotalEnqueued.fetch_add(1, std::memory_order_relaxed);

        // English: Wake up the target worker only
        // 한글: 대상 워커만 깨우기
        wq->queueCV.notify_one();
    }

    size_t OrderedTaskQueue::GetWorkerQueueSize(size_t workerIndex) const
    {
        if (workerIndex >= mWorkerQueues.size())
            return 0;
        return mWorkerQueues[workerIndex]->queueSize.load(std::memory_order_relaxed);
    }

    void OrderedTaskQueue::WorkerThreadFunc(size_t workerIndex)
    {
        Logger::Info("OrderedTaskQueue worker[" + std::to_string(workerIndex) + "] started");

        auto& wq = mWorkerQueues[workerIndex];

        while (mIsRunning.load())
        {
            OrderedTask task;
            bool hasTask = false;

            // English: Wait for task or shutdown signal
            // 한글: 작업 또는 종료 신호 대기
            {
                std::unique_lock<std::mutex> lock(wq->queueMutex);

                wq->queueCV.wait(lock, [&wq, this] {
                    return !wq->taskQueue.empty() || !mIsRunning.load();
                });

                if (!wq->taskQueue.empty())
                {
                    task = std::move(wq->taskQueue.front());
                    wq->taskQueue.pop();
                    wq->queueSize.fetch_sub(1, std::memory_order_relaxed);
                    hasTask = true;
                }
            }

            // English: Execute task outside of lock
            // 한글: 락 밖에서 작업 실행
            if (hasTask)
            {
                try
                {
                    if (task.taskFunc)
                    {
                        task.taskFunc();
                    }
                    mTotalProcessed.fetch_add(1, std::memory_order_relaxed);
                }
                catch (const std::exception& e)
                {
                    mTotalFailed.fetch_add(1, std::memory_order_relaxed);
                    Logger::Error("OrderedTaskQueue worker[" + std::to_string(workerIndex) +
                                  "] task exception: " + std::string(e.what()));
                }
            }
        }

        // English: Drain remaining tasks before exit
        // 한글: 종료 전 남은 작업 처리
        {
            std::lock_guard<std::mutex> lock(wq->queueMutex);
            while (!wq->taskQueue.empty())
            {
                auto remainingTask = std::move(wq->taskQueue.front());
                wq->taskQueue.pop();
                wq->queueSize.fetch_sub(1, std::memory_order_relaxed);

                try
                {
                    if (remainingTask.taskFunc)
                    {
                        remainingTask.taskFunc();
                    }
                    mTotalProcessed.fetch_add(1, std::memory_order_relaxed);
                }
                catch (const std::exception& e)
                {
                    mTotalFailed.fetch_add(1, std::memory_order_relaxed);
                    Logger::Error("OrderedTaskQueue worker[" + std::to_string(workerIndex) +
                                  "] drain task exception: " + std::string(e.what()));
                }
            }
        }

        Logger::Info("OrderedTaskQueue worker[" + std::to_string(workerIndex) + "] stopped");
    }

    size_t OrderedTaskQueue::KeyToWorkerIndex(uint32_t key) const
    {
        // English: Simple modulo hash - same serverId always goes to same worker
        //          This ensures FIFO ordering per serverId.
        //          Different serverIds may share a worker but still execute independently in order.
        // 한글: 단순 모듈러 해시 - 같은 serverId는 항상 같은 워커로 배정
        //       이를 통해 serverId별 FIFO 순서를 보장
        //       다른 serverId가 같은 워커를 공유할 수 있지만 순서는 독립적으로 유지
        return static_cast<size_t>(key) % mWorkerCount;
    }

} // namespace Network::DBServer
