#pragma once

// Asynchronous DB task queue - separates game logic from database operations

// Forward-declare IDatabase to avoid pulling in ServerEngine headers here
namespace Network { namespace Database { class IDatabase; } }

#include "Utils/NetworkUtils.h"
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace Network::TestServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // DB task types
    // =============================================================================

    enum class DBTaskType : uint8_t
    {
        RecordConnectTime,
        RecordDisconnectTime,
        UpdatePlayerData,
    };

    // =============================================================================
    // DB task data
    // =============================================================================

    struct DBTask
    {
        DBTaskType type;
        ConnectionId sessionId;
        std::string data;
        std::function<void(bool success, const std::string& result)> callback;
        uint64_t walSeq = 0;  // WAL sequence (0 = not WAL-tracked, e.g. recovered tasks)

        DBTask(DBTaskType t, ConnectionId id, std::string d = "")
            : type(t), sessionId(id), data(std::move(d)), callback(nullptr)
        {
        }

        DBTask(DBTaskType t, ConnectionId id, std::string d,
               std::function<void(bool, const std::string&)> cb)
            : type(t), sessionId(id), data(std::move(d)), callback(std::move(cb))
        {
        }
    };

    // =============================================================================
    // Asynchronous DB task queue with key-affinity routing.
    //
    // Per-session ordering guarantee:
    //   Each task is routed to worker[sessionId % workerCount].
    //   The same session always maps to the same worker.
    //   Each worker is a single thread processing tasks FIFO, so:
    //     (1) Tasks for the same session always execute on the same worker.
    //     (2) Task B is never sent to DB until task A (queued before B for
    //         the same session) has fully completed — the worker pops and
    //         processes one task at a time.
    //
    // =============================================================================

    class DBTaskQueue
    {
    public:
        DBTaskQueue();
        ~DBTaskQueue();

        // Lifecycle
        bool Initialize(size_t workerThreadCount = 1,
                        const std::string& walPath = "db_tasks.wal",
                        Network::Database::IDatabase* db = nullptr);
        void Shutdown();
        bool IsRunning() const;

        // Task submission (non-blocking, move semantics)
        void EnqueueTask(DBTask&& task);

        // Convenience methods for common operations
        void RecordConnectTime(ConnectionId sessionId, const std::string& timestamp);
        void RecordDisconnectTime(ConnectionId sessionId, const std::string& timestamp);
        void UpdatePlayerData(ConnectionId sessionId, const std::string& jsonData,
                              std::function<void(bool, const std::string&)> callback = nullptr);

        // Statistics
        size_t GetQueueSize() const;
        size_t GetProcessedCount() const;
        size_t GetFailedCount() const;

    private:
        // Worker thread function
        void WorkerThreadFunc(size_t workerIndex);

        // Process individual task
        bool ProcessTask(const DBTask& task);

        // Specific task handlers
        bool HandleRecordConnectTime(const DBTask& task, std::string& result);
        bool HandleRecordDisconnectTime(const DBTask& task, std::string& result);
        bool HandleUpdatePlayerData(const DBTask& task, std::string& result);

        // =====================================================================
        // WAL (Write-Ahead Log) for crash recovery
        //
        // Format per line:
        //   P|<TYPE>|<SESSIONID>|<SEQ>|<DATA>   (Pending)
        //   D|<SEQ>                             (Done)
        //   STATUS: P(Pending) or D(Done)
        //   TYPE: DBTaskType as integer
        //   SEQ: monotonic sequence number for matching P/D pairs
        // =====================================================================
        void     WalWritePending(const DBTask& task, uint64_t seq);
        void     WalWriteDone(uint64_t seq);
        void     WalRecover();
        uint64_t WalNextSeq();
        // Open WAL file if not already open. Must be called under mWalMutex.
        //          Returns true if the file is open (or was opened successfully).
        bool     EnsureWalOpen();

    private:
        // Per-worker data — each worker owns its queue, mutex, cv, and thread.
        //   Routing: sessionId % workerCount → same session always → same worker.
        //   Within a worker: single thread + FIFO → task B is never dequeued until task A completes.
        struct WorkerData
        {
            std::queue<DBTask>      taskQueue;
            mutable std::mutex      mutex;
            std::condition_variable cv;
            std::thread             thread;
        };

        std::vector<std::unique_ptr<WorkerData>> mWorkers;

        // Global queue size counter across all workers (lock-free GetQueueSize)
        std::atomic<size_t>             mQueueSize;

        std::atomic<bool>               mIsRunning;

        // Statistics
        std::atomic<size_t>             mProcessedCount;
        std::atomic<size_t>             mFailedCount;

        // WAL crash-recovery members
        std::string                     mWalPath;
        std::ofstream                   mWalFile;
        mutable std::mutex              mWalMutex;
        std::atomic<uint64_t>           mWalSeq{0};

        // Injected database (non-owning); nullptr = log-only mode
        Network::Database::IDatabase* mDatabase = nullptr;
    };

} // namespace Network::TestServer
