// English: Asynchronous DB task queue implementation
// 한글: 비동기 DB 작업 큐 구현

#include "../include/DBTaskQueue.h"
#include "Utils/Logger.h"
#include <chrono>
#include <vector>

#ifdef ENABLE_DATABASE_SUPPORT
#include "Database/DatabaseModule.h"
using namespace Network::Database;
#endif

namespace Network::TestServer
{
    //using namespace Network::Core;
    using namespace Network::Utils;

    // =============================================================================
    // English: DBTaskQueue implementation
    // 한글: DBTaskQueue 구현
    // =============================================================================

    DBTaskQueue::DBTaskQueue()
        : mQueueSize(0)
        , mIsRunning(false)
        , mProcessedCount(0)
        , mFailedCount(0)
    {
    }

    DBTaskQueue::~DBTaskQueue()
    {
        if (mIsRunning.load())
        {
            Shutdown();
        }
    }

    bool DBTaskQueue::Initialize(size_t workerThreadCount)
    {
        if (mIsRunning.load())
        {
            Logger::Warn("DBTaskQueue already running");
            return true;
        }

        Logger::Info("Initializing DBTaskQueue with " + std::to_string(workerThreadCount) + " worker threads");

        // English: Warn about multi-worker ordering limitation
        // 한글: 멀티워커 순서 보장 제한 경고
        if (workerThreadCount > 1)
        {
            Logger::Warn("DBTaskQueue: workerThreadCount > 1 - per-sessionId task ordering is NOT guaranteed. "
                         "Consider using OrderedTaskQueue for ordered processing.");
        }

        mIsRunning.store(true);

        // English: Start worker threads
        // 한글: 워커 스레드 시작
        for (size_t i = 0; i < workerThreadCount; ++i)
        {
            mWorkerThreads.emplace_back(&DBTaskQueue::WorkerThreadFunc, this);
        }

        Logger::Info("DBTaskQueue initialized successfully");
        return true;
    }

    void DBTaskQueue::Shutdown()
    {
        if (!mIsRunning.load())
        {
            return;
        }

        Logger::Info("Shutting down DBTaskQueue...");

        // English: Signal all worker threads to stop
        // 한글: 모든 워커 스레드에 중지 신호 전송
        mIsRunning.store(false);
        mQueueCV.notify_all();

        // English: Wait for all worker threads to finish
        // 한글: 모든 워커 스레드가 종료될 때까지 대기
        for (auto& thread : mWorkerThreads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        mWorkerThreads.clear();

        // English: Drain remaining tasks before clearing (execute pending work + invoke failure callbacks)
        // 한글: 제거 전 남은 작업 처리 (대기 중인 작업 실행 + 실패 콜백 호출)
        {
            std::vector<DBTask> drainTasks;
            {
                std::lock_guard<std::mutex> lock(mQueueMutex);
                size_t remaining = mTaskQueue.size();
                if (remaining > 0)
                {
                    Logger::Warn("DBTaskQueue draining " + std::to_string(remaining) + " remaining tasks before shutdown");
                }
                while (!mTaskQueue.empty())
                {
                    drainTasks.push_back(std::move(mTaskQueue.front()));
                    mTaskQueue.pop();
                }
                mQueueSize.store(0, std::memory_order_relaxed);
            }

            // English: Execute drained tasks outside of lock
            // 한글: 수집된 작업을 락 밖에서 실행
            for (auto& task : drainTasks)
            {
                try
                {
                    ProcessTask(task);
                }
                catch (const std::exception& e)
                {
                    Logger::Error("DBTaskQueue drain task exception: " + std::string(e.what()));
                    if (task.callback)
                    {
                        task.callback(false, std::string("Drain exception: ") + e.what());
                    }
                }
            }
        }

        Logger::Info("DBTaskQueue shutdown complete - Processed: " +
                     std::to_string(mProcessedCount.load()) +
                     ", Failed: " + std::to_string(mFailedCount.load()));
    }

    bool DBTaskQueue::IsRunning() const
    {
        return mIsRunning.load();
    }

    void DBTaskQueue::EnqueueTask(DBTask&& task)
    {
        if (!mIsRunning.load())
        {
            Logger::Error("Cannot enqueue task - DBTaskQueue not running");
            if (task.callback)
            {
                task.callback(false, "DBTaskQueue not running");
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            mTaskQueue.push(std::move(task));

            // English: Increment queue size atomically (enables lock-free GetQueueSize)
            // 한글: Atomic으로 큐 크기 증가 (lock-free GetQueueSize 가능)
            mQueueSize.fetch_add(1, std::memory_order_relaxed);
        }

        // English: Notify one worker thread
        // 한글: 워커 스레드 하나에 알림
        mQueueCV.notify_one();
    }

    void DBTaskQueue::RecordConnectTime(ConnectionId sessionId, const std::string& timestamp)
    {
        // English: Non-blocking enqueue with move semantics
        // 한글: 이동 의미론을 사용한 논블로킹 큐잉
        EnqueueTask(DBTask(DBTaskType::RecordConnectTime, sessionId, timestamp));

        Logger::Debug("Enqueued RecordConnectTime task for Session: " + std::to_string(sessionId));
    }

    void DBTaskQueue::RecordDisconnectTime(ConnectionId sessionId, const std::string& timestamp)
    {
        // English: Move temporary DBTask object (avoid copy)
        // 한글: 임시 DBTask 객체를 이동 (복사 방지)
        EnqueueTask(DBTask(DBTaskType::RecordDisconnectTime, sessionId, timestamp));

        Logger::Debug("Enqueued RecordDisconnectTime task for Session: " + std::to_string(sessionId));
    }

    void DBTaskQueue::UpdatePlayerData(ConnectionId sessionId, const std::string& jsonData,
                                       std::function<void(bool, const std::string&)> callback)
    {
        // English: Move temporary DBTask object with callback
        // 한글: 콜백과 함께 임시 DBTask 객체 이동
        EnqueueTask(DBTask(DBTaskType::UpdatePlayerData, sessionId, jsonData, callback));

        Logger::Debug("Enqueued UpdatePlayerData task for Session: " + std::to_string(sessionId));
    }

    size_t DBTaskQueue::GetQueueSize() const
    {
        // English: Lock-free queue size query (optimization)
        // 한글: Lock-free 큐 크기 조회 (최적화)
        // Performance: Atomic load is ~10-100x faster than mutex acquisition
        // 성능: Atomic load는 mutex 획득보다 약 10-100배 빠름
        // Note: May be slightly inaccurate due to concurrent operations, but acceptable for statistics
        // 참고: 동시 작업으로 인해 약간 부정확할 수 있지만 통계용으로는 충분함
        return mQueueSize.load(std::memory_order_relaxed);
    }

    size_t DBTaskQueue::GetProcessedCount() const
    {
        return mProcessedCount.load();
    }

    size_t DBTaskQueue::GetFailedCount() const
    {
        return mFailedCount.load();
    }

    void DBTaskQueue::WorkerThreadFunc()
    {
        Logger::Info("DBTaskQueue worker thread started");

        while (mIsRunning.load())
        {
            DBTask task(DBTaskType::Custom, 0);
            bool hasTask = false;

            // English: Wait for task or shutdown signal
            // 한글: 작업 또는 종료 신호 대기
            {
                std::unique_lock<std::mutex> lock(mQueueMutex);

                mQueueCV.wait(lock, [this] {
                    return !mTaskQueue.empty() || !mIsRunning.load();
                });

                if (!mTaskQueue.empty())
                {
                    task = std::move(mTaskQueue.front());
                    mTaskQueue.pop();

                    // English: Decrement queue size atomically
                    // 한글: Atomic으로 큐 크기 감소
                    mQueueSize.fetch_sub(1, std::memory_order_relaxed);

                    hasTask = true;
                }
            }

            // English: Process task outside of lock
            // 한글: 락 외부에서 작업 처리
            if (hasTask)
            {
                ProcessTask(task);
            }
        }

        Logger::Info("DBTaskQueue worker thread stopped");
    }

    void DBTaskQueue::ProcessTask(const DBTask& task)
    {
        bool success = false;
        std::string result;

        try
        {
            switch (task.type)
            {
            case DBTaskType::RecordConnectTime:
                success = HandleRecordConnectTime(task, result);
                break;

            case DBTaskType::RecordDisconnectTime:
                success = HandleRecordDisconnectTime(task, result);
                break;

            case DBTaskType::UpdatePlayerData:
                success = HandleUpdatePlayerData(task, result);
                break;

            default:
                result = "Unknown task type";
                Logger::Error("Unknown DB task type");
                break;
            }

            if (success)
            {
                mProcessedCount.fetch_add(1);
            }
            else
            {
                mFailedCount.fetch_add(1);
            }
        }
        catch (const std::exception& e)
        {
            success = false;
            result = std::string("Exception: ") + e.what();
            mFailedCount.fetch_add(1);
            Logger::Error("DB task exception: " + result);
        }

        // English: Invoke callback if provided
        // 한글: 콜백이 제공된 경우 호출
        if (task.callback)
        {
            task.callback(success, result);
        }
    }

    bool DBTaskQueue::HandleRecordConnectTime(const DBTask& task, std::string& result)
    {
#ifdef ENABLE_DATABASE_SUPPORT
        try
        {
            // TODO: Use actual ConnectionPool when available
            // For now, just log the operation
            Logger::Info("DB: Record connect time for Session " +
                        std::to_string(task.sessionId) + " at " + task.data);

            result = "Connect time recorded (simulated)";
            return true;

            /* Example with actual DB:
            ConnectionPool& pool = GetGlobalConnectionPool();
            auto conn = pool.getConnection();
            auto stmt = conn->createStatement();
            stmt->setQuery("INSERT INTO SessionLog (SessionId, ConnectTime) VALUES (?, ?)");
            stmt->bindParameter(1, static_cast<int>(task.sessionId));
            stmt->bindParameter(2, task.data);
            int rows = stmt->executeUpdate();
            pool.returnConnection(conn);
            return rows > 0;
            */
        }
        catch (const std::exception& e)
        {
            result = std::string("DB error: ") + e.what();
            Logger::Error("Failed to record connect time: " + result);
            return false;
        }
#else
        Logger::Info("Database support disabled - Session " +
                    std::to_string(task.sessionId) + " connected at " + task.data);
        result = "DB support disabled";
        return true;
#endif
    }

    bool DBTaskQueue::HandleRecordDisconnectTime(const DBTask& task, std::string& result)
    {
#ifdef ENABLE_DATABASE_SUPPORT
        Logger::Info("DB: Record disconnect time for Session " +
                    std::to_string(task.sessionId) + " at " + task.data);

        result = "Disconnect time recorded (simulated)";
        return true;
#else
        Logger::Info("Database support disabled - Session " +
                    std::to_string(task.sessionId) + " disconnected at " + task.data);
        result = "DB support disabled";
        return true;
#endif
    }

    bool DBTaskQueue::HandleUpdatePlayerData(const DBTask& task, std::string& result)
    {
#ifdef ENABLE_DATABASE_SUPPORT
        Logger::Info("DB: Update player data for Session " +
                    std::to_string(task.sessionId) + " - Data: " + task.data);

        result = "Player data updated (simulated)";
        return true;
#else
        Logger::Info("Database support disabled - Player data for Session " +
                    std::to_string(task.sessionId));
        result = "DB support disabled";
        return true;
#endif
    }

} // namespace Network::TestServer
