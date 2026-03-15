// Asynchronous DB task queue implementation

#include "../include/DBTaskQueue.h"

#include "Utils/Logger.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

// IDatabase interface always available for runtime injection
#include "Interfaces/IDatabase.h"
#include "Interfaces/IStatement.h"

#ifdef ENABLE_DATABASE_SUPPORT
#include "Database/DatabaseModule.h"
#endif

namespace Network::TestServer
{

using namespace Network::Utils;

// =============================================================================
// DBTaskQueue implementation
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

bool DBTaskQueue::Initialize(size_t workerThreadCount,
                             const std::string& walPath,
                             Network::Database::IDatabase* db)
{
    if (mIsRunning.load())
    {
        Logger::Warn("DBTaskQueue already running");
        return true;
    }

    Logger::Info("Initializing DBTaskQueue with " +
                 std::to_string(workerThreadCount) + " worker threads");

    // Multi-worker is safe: sessionId % workerCount routes each session
    //          to a dedicated worker. Single-threaded FIFO per worker ensures
    //          per-session ordering and serial DB execution.

    // Store injected database reference and create tables if DB available
    mDatabase = db;

    if (mDatabase != nullptr && mDatabase->IsConnected())
    {
        static const char* kCreateTableSQLs[] = {
            "CREATE TABLE IF NOT EXISTS SessionConnectLog ("
            "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  session_id  INTEGER NOT NULL,"
            "  connect_time TEXT NOT NULL"
            ")",
            "CREATE TABLE IF NOT EXISTS SessionDisconnectLog ("
            "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  session_id     INTEGER NOT NULL,"
            "  disconnect_time TEXT NOT NULL"
            ")",
            "CREATE TABLE IF NOT EXISTS PlayerData ("
            "  session_id INTEGER PRIMARY KEY,"
            "  data       TEXT"
            ")"
        };

        for (const char* sql : kCreateTableSQLs)
        {
            try
            {
                auto stmt = mDatabase->CreateStatement();
                stmt->SetQuery(sql);
                stmt->Execute();
            }
            catch (const std::exception& e)
            {
                Logger::Warn("DBTaskQueue: Failed to create table: " +
                             std::string(e.what()));
            }
        }

        Logger::Info("DBTaskQueue: DB tables ensured (SessionConnectLog, "
                     "SessionDisconnectLog, PlayerData)");
    }

    // Start workers BEFORE WAL recovery so EnqueueTask() accepts recovered tasks
    mIsRunning.store(true);

    // Create per-worker data structures first, then start threads.
    for (size_t i = 0; i < workerThreadCount; ++i)
    {
        mWorkers.push_back(std::make_unique<WorkerData>());
    }

    for (size_t i = 0; i < mWorkers.size(); ++i)
    {
        mWorkers[i]->thread = std::thread(&DBTaskQueue::WorkerThreadFunc, this, i);
    }

    // Set WAL path and re-enqueue tasks from previous crash (if any)
    mWalPath = walPath;
    WalRecover();

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

    // Signal all worker threads to stop
    mIsRunning.store(false);

    // Wake up every per-worker CV so threads can exit their wait loop.
    for (auto& worker : mWorkers)
    {
        worker->cv.notify_all();
    }

    // Wait for all worker threads to finish.
    for (auto& worker : mWorkers)
    {
        if (worker->thread.joinable())
        {
            worker->thread.join();
        }
    }

    // Drain remaining tasks from all per-worker queues.
    //          All workers joined; drain without lock.
    for (auto& worker : mWorkers)
    {
        std::vector<DBTask> drainTasks;
        while (!worker->taskQueue.empty())
        {
            drainTasks.push_back(std::move(worker->taskQueue.front()));
            worker->taskQueue.pop();
        }

        if (!drainTasks.empty())
        {
            Logger::Warn("DBTaskQueue draining " +
                         std::to_string(drainTasks.size()) +
                         " remaining tasks before shutdown");
        }

        // Execute drained tasks outside of lock.
        for (auto& task : drainTasks)
        {
            try
            {
                const bool success = ProcessTask(task);

                // Keep WAL semantics identical to worker path.
                if (success && task.walSeq != 0)
                {
                    WalWriteDone(task.walSeq);
                }
            }
            catch (const std::exception& e)
            {
                Logger::Error("DBTaskQueue drain task exception: " +
                              std::string(e.what()));
                if (task.callback)
                {
                    task.callback(false, std::string("Drain exception: ") + e.what());
                }
            }
        }
    }

    mQueueSize.store(0, std::memory_order_relaxed);

    Logger::Info("DBTaskQueue shutdown complete - Processed: " +
                 std::to_string(mProcessedCount.load()) +
                 ", Failed: " + std::to_string(mFailedCount.load()));
}

bool DBTaskQueue::IsRunning() const { return mIsRunning.load(); }

void DBTaskQueue::EnqueueTask(DBTask&& task)
{
    if (!mIsRunning.load(std::memory_order_acquire))
    {
        Logger::Error("Cannot enqueue task - DBTaskQueue not running");
        if (task.callback)
        {
            task.callback(false, "DBTaskQueue not running");
        }
        return;
    }

    // WAL - record pending task before queueing (crash-safe)
    if (task.walSeq == 0)
    {
        task.walSeq = WalNextSeq();
        WalWritePending(task, task.walSeq);
    }

    // Key-affinity routing: sessionId % workerCount.
    //          Same session → same worker → FIFO serial processing guaranteed.
    const size_t workerIndex = static_cast<size_t>(task.sessionId) % mWorkers.size();
    WorkerData& worker = *mWorkers[workerIndex];

    bool     accepted        = false;
    bool     shouldWriteDone = false;
    uint64_t canceledWalSeq  = 0;

    {
        std::lock_guard<std::mutex> lock(worker.mutex);

        // Re-check under queue lock to close the Shutdown() race window.
        if (mIsRunning.load(std::memory_order_acquire))
        {
            worker.taskQueue.push(std::move(task));

            // Increment global queue size counter (lock-free GetQueueSize)
            mQueueSize.fetch_add(1, std::memory_order_relaxed);
            accepted = true;
        }
        else if (task.walSeq != 0)
        {
            // Task was WAL-pended but never queued. Mark done to avoid replay.
            shouldWriteDone = true;
            canceledWalSeq  = task.walSeq;
        }
    }

    // Check accepted BEFORE accessing task fields.
    //          task was moved into the worker queue (push(std::move(task))) inside the
    //          lock above, so it is in a moved-from state on the accepted path.
    //          Accessing task.walSeq or task.callback after this point is only safe in
    //          the rejected path (accepted == false) where the move never happened.
    //          Note: the original code had this condition inverted (!accepted guarded the
    //          success path), causing notify_one to be called on error. This is the fix.
    if (accepted)
    {
        worker.cv.notify_one();
        return;
    }

    // Reached only in the rejected path — task was NOT moved, so
    //          task.walSeq and task.callback are still valid to access.
    if (shouldWriteDone)
    {
        WalWriteDone(canceledWalSeq);
    }

    Logger::Error("Cannot enqueue task - DBTaskQueue shutting down");
    if (task.callback)
    {
        task.callback(false, "DBTaskQueue shutting down");
    }
}

void DBTaskQueue::RecordConnectTime(ConnectionId sessionId, const std::string& timestamp)
{
    // Non-blocking enqueue with move semantics
    EnqueueTask(DBTask(DBTaskType::RecordConnectTime, sessionId, timestamp));
    Logger::Debug("Enqueued RecordConnectTime task for Session: " +
                  std::to_string(sessionId));
}

void DBTaskQueue::RecordDisconnectTime(ConnectionId sessionId, const std::string& timestamp)
{
    // Move temporary DBTask object (avoid copy)
    EnqueueTask(DBTask(DBTaskType::RecordDisconnectTime, sessionId, timestamp));
    Logger::Debug("Enqueued RecordDisconnectTime task for Session: " +
                  std::to_string(sessionId));
}

void DBTaskQueue::UpdatePlayerData(ConnectionId sessionId, const std::string& jsonData,
                                   std::function<void(bool, const std::string&)> callback)
{
    // Move temporary DBTask object with callback
    EnqueueTask(DBTask(DBTaskType::UpdatePlayerData, sessionId, jsonData, callback));
    Logger::Debug("Enqueued UpdatePlayerData task for Session: " +
                  std::to_string(sessionId));
}

size_t DBTaskQueue::GetQueueSize() const
{
    // Lock-free queue size query (optimization)
    // Performance: Atomic load is ~10-100x faster than mutex acquisition
    // Note: May be slightly inaccurate due to concurrent operations, but acceptable for statistics
    return mQueueSize.load(std::memory_order_relaxed);
}

size_t DBTaskQueue::GetProcessedCount() const { return mProcessedCount.load(); }
size_t DBTaskQueue::GetFailedCount() const { return mFailedCount.load(); }

void DBTaskQueue::WorkerThreadFunc(size_t workerIndex)
{
    Logger::Info("DBTaskQueue worker[" + std::to_string(workerIndex) + "] thread started");

    WorkerData& worker = *mWorkers[workerIndex];

    while (mIsRunning.load())
    {
        DBTask task(DBTaskType::RecordConnectTime, 0);
        bool   hasTask = false;

        // Wait on this worker's own CV — not a shared global CV.
        //          This ensures only this worker is woken up when its queue
        //          receives a task (sessionId % workerCount == workerIndex).
        {
            std::unique_lock<std::mutex> lock(worker.mutex);
            worker.cv.wait(lock, [&] {
                return !worker.taskQueue.empty() || !mIsRunning.load();
            });

            if (!worker.taskQueue.empty())
            {
                task = std::move(worker.taskQueue.front());
                worker.taskQueue.pop();

                // Decrement global queue size counter.
                mQueueSize.fetch_sub(1, std::memory_order_relaxed);
                hasTask = true;
            }
        }

        // Process task outside of lock — DB I/O is blocking but does
        //          not hold the queue lock, so new tasks can be enqueued freely
        //          while this task is executing. The next task for the same
        //          session will not be dequeued until this one returns.
        if (hasTask)
        {
            const bool success = ProcessTask(task);

            // WAL - mark task as done after successful processing.
            if (success && task.walSeq != 0)
            {
                WalWriteDone(task.walSeq);
            }
        }
    }

    Logger::Info("DBTaskQueue worker[" + std::to_string(workerIndex) + "] thread stopped");
}

bool DBTaskQueue::ProcessTask(const DBTask& task)
{
    bool        success = false;
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
            result = "Unknown DB task type: " + std::to_string(static_cast<int>(task.type));
            Logger::Error("Unknown DB task type: " + std::to_string(static_cast<int>(task.type)));
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
        result  = std::string("Exception: ") + e.what();
        mFailedCount.fetch_add(1);
        Logger::Error("DB task exception: " + result);
    }

    // Invoke callback if provided
    if (task.callback)
    {
        task.callback(success, result);
    }

    return success;
}

bool DBTaskQueue::HandleRecordConnectTime(const DBTask& task, std::string& result)
{
    // null means DB was never configured — hard error (not log-only mode).
    //          IsConnected() == false means configured but temporarily offline — acceptable,
    //          falls through to log-only path below.
    if (mDatabase == nullptr)
    {
        Logger::Warn("HandleRecordConnectTime: mDatabase is null");
        result = "Database pointer is null";
        return false;
    }

    if (mDatabase->IsConnected())
    {
        try
        {
            auto stmt = mDatabase->CreateStatement();
            stmt->SetQuery("INSERT INTO SessionConnectLog (session_id, connect_time) VALUES (?, ?)");
            stmt->BindParameter(1, static_cast<long long>(task.sessionId));
            stmt->BindParameter(2, task.data);
            stmt->ExecuteUpdate();
            Logger::Info("DB INSERT SessionConnectLog - Session: " +
                         std::to_string(task.sessionId) + " at " + task.data);
            result = "Connect time recorded to DB";
            return true;
        }
        catch (const std::exception& e)
        {
            result = std::string("DB error: ") + e.what();
            Logger::Error("HandleRecordConnectTime failed: " + result);
            return false;
        }
    }

    // Fallback — log only (no DB connected)
    Logger::Info("Session " + std::to_string(task.sessionId) + " connected at " + task.data);
    result = "Connect time logged (no DB)";
    return true;
}

bool DBTaskQueue::HandleRecordDisconnectTime(const DBTask& task, std::string& result)
{
    if (mDatabase == nullptr)
    {
        Logger::Warn("HandleRecordDisconnectTime: mDatabase is null");
        result = "Database pointer is null";
        return false;
    }

    if (mDatabase->IsConnected())
    {
        try
        {
            auto stmt = mDatabase->CreateStatement();
            stmt->SetQuery("INSERT INTO SessionDisconnectLog (session_id, disconnect_time) VALUES (?, ?)");
            stmt->BindParameter(1, static_cast<long long>(task.sessionId));
            stmt->BindParameter(2, task.data);
            stmt->ExecuteUpdate();
            Logger::Info("DB INSERT SessionDisconnectLog - Session: " +
                         std::to_string(task.sessionId) + " at " + task.data);
            result = "Disconnect time recorded to DB";
            return true;
        }
        catch (const std::exception& e)
        {
            result = std::string("DB error: ") + e.what();
            Logger::Error("HandleRecordDisconnectTime failed: " + result);
            return false;
        }
    }

    Logger::Info("Session " + std::to_string(task.sessionId) + " disconnected at " + task.data);
    result = "Disconnect time logged (no DB)";
    return true;
}

bool DBTaskQueue::HandleUpdatePlayerData(const DBTask& task, std::string& result)
{
    if (mDatabase == nullptr)
    {
        Logger::Warn("HandleUpdatePlayerData: mDatabase is null");
        result = "Database pointer is null";
        return false;
    }

    if (mDatabase->IsConnected())
    {
        try
        {
            auto stmt = mDatabase->CreateStatement();

            // Upsert — insert or replace player data
            stmt->SetQuery("INSERT OR REPLACE INTO PlayerData (session_id, data) VALUES (?, ?)");
            stmt->BindParameter(1, static_cast<long long>(task.sessionId));
            stmt->BindParameter(2, task.data);
            stmt->ExecuteUpdate();
            Logger::Info("DB UPSERT PlayerData - Session: " + std::to_string(task.sessionId));
            result = "Player data updated to DB";
            return true;
        }
        catch (const std::exception& e)
        {
            result = std::string("DB error: ") + e.what();
            Logger::Error("HandleUpdatePlayerData failed: " + result);
            return false;
        }
    }

    Logger::Info("Player data for Session " + std::to_string(task.sessionId) +
                 " (no DB): " + task.data);
    result = "Player data logged (no DB)";
    return true;
}

// =============================================================================
// WAL (Write-Ahead Log) crash recovery implementation
//
// Format per line: <STATUS>|<TYPE>|<SESSIONID>|<SEQ>|<DATA>
//   P = Pending (written before enqueue)
//   D = Done    (written after successful ProcessTask)
// =============================================================================

uint64_t DBTaskQueue::WalNextSeq()
{
    // Monotonically increasing, unique sequence number
    return mWalSeq.fetch_add(1, std::memory_order_relaxed) + 1;
}

bool DBTaskQueue::EnsureWalOpen()
{
    // Caller MUST hold mWalMutex before calling this function.
    if (mWalFile.is_open())
    {
        return true;
    }
    mWalFile.open(mWalPath, std::ios::app | std::ios::out);
    return mWalFile.is_open();
}

void DBTaskQueue::WalWritePending(const DBTask& task, uint64_t seq)
{
    if (mWalPath.empty())
    {
        return;
    }

    // mWalFile lazy-open is protected by mWalMutex to prevent race conditions.
    std::lock_guard<std::mutex> lock(mWalMutex);
    if (!EnsureWalOpen())
    {
        Logger::Warn("WAL: Failed to open WAL file: " + mWalPath);
        return;
    }

    // Escape '|' in data field to avoid parse ambiguity
    std::string escapedData = task.data;
    for (auto& c : escapedData)
    {
        if (c == '|') c = '\x01'; // substitute char
    }

    // P|<type>|<sessionId>|<seq>|<data>
    mWalFile << "P|" << static_cast<int>(task.type)
             << "|" << task.sessionId
             << "|" << seq
             << "|" << escapedData
             << "\n";
    mWalFile.flush();
}

void DBTaskQueue::WalWriteDone(uint64_t seq)
{
    if (mWalPath.empty())
    {
        return;
    }

    // mWalFile lazy-open is protected by mWalMutex to prevent race conditions.
    std::lock_guard<std::mutex> lock(mWalMutex);
    if (!EnsureWalOpen())
    {
        return;
    }

    // D|<seq>
    mWalFile << "D|" << seq << "\n";
    mWalFile.flush();
}

void DBTaskQueue::WalRecover()
{
    if (mWalPath.empty())
    {
        return;
    }

    // Backup file suffix — defined once here, used in both the read phase
    //          (parseWalFile) and the write phase (rename + recovery).
    const std::string kWalBackupSuffix = ".bak";

    // WAL entry struct (used for both primary and backup file parsing).
    struct WalEntry
    {
        DBTaskType   type;
        ConnectionId sessionId;
        std::string  data;
    };

    // Keep pending tasks ordered by sequence for deterministic replay.
    std::map<uint64_t, WalEntry> pendingMap;
    std::string                  line;
    size_t                       recoveredCount = 0;
    uint64_t                     maxSeenSeq     = 0;

    // Helper lambda — parse one WAL file (primary or .bak) into pendingMap.
    //          Called for both files so that a crash during rename-and-re-enqueue does
    //          not permanently lose tasks that only appear in the backup.
    auto parseWalFile = [&](const std::string& path)
    {
        std::ifstream f(path);
        if (!f.is_open()) return;

        while (std::getline(f, line))
        {
            if (line.empty()) continue;

            std::istringstream ss(line);
            std::string        status;
            if (!std::getline(ss, status, '|')) continue;

            if (status == "P")
            {
                std::string typeStr, sessionStr, seqStr, data;
                if (!std::getline(ss, typeStr,    '|')) continue;
                if (!std::getline(ss, sessionStr, '|')) continue;
                if (!std::getline(ss, seqStr,     '|')) continue;
                std::getline(ss, data);

                uint64_t seq = 0;
                try { seq = std::stoull(seqStr); }
                catch (...) { continue; }

                if (seq > maxSeenSeq) maxSeenSeq = seq;

                for (auto& c : data)
                {
                    if (c == '\x01') c = '|';
                }

                int typeInt = 0;
                try { typeInt = std::stoi(typeStr); }
                catch (...) { continue; }

                ConnectionId sessionId = 0;
                try { sessionId = static_cast<ConnectionId>(std::stoull(sessionStr)); }
                catch (...) { continue; }

                WalEntry entry;
                entry.type      = static_cast<DBTaskType>(typeInt);
                entry.sessionId = sessionId;
                entry.data      = data;

                // Higher-seq entry wins if both files have the same seq.
                pendingMap.emplace(seq, std::move(entry));
            }
            else if (status == "D")
            {
                std::string seqStr;
                if (!std::getline(ss, seqStr, '|')) continue;

                uint64_t seq = 0;
                try { seq = std::stoull(seqStr); }
                catch (...) { continue; }

                if (seq > maxSeenSeq) maxSeenSeq = seq;
                pendingMap.erase(seq);
            }
        }
    };

    // Read primary WAL and backup (if present) so we never lose tasks
    //          regardless of which crash scenario occurred.
    parseWalFile(mWalPath);
    parseWalFile(mWalPath + kWalBackupSuffix);

    if (pendingMap.empty() && maxSeenSeq == 0)
    {
        // No WAL file found at all = clean startup
        return;
    }

    // Keep sequence monotonic after restart.
    mWalSeq.store(maxSeenSeq, std::memory_order_relaxed);

    if (pendingMap.empty())
    {
        // WAL existed but all tasks were completed — remove it (and backup if any).
        std::remove(mWalPath.c_str());
        std::remove((mWalPath + kWalBackupSuffix).c_str());
        Logger::Info("WAL: Clean startup (no pending tasks to recover)");
        return;
    }

    Logger::Warn("WAL: Recovering " + std::to_string(pendingMap.size()) +
                 " unfinished task(s) from previous crash");

    // WAL crash-safe recovery order:
    //   1. Rename old WAL → backup  (atomic; preserves data if we crash mid-recovery)
    //   2. Re-enqueue each task      (WalWritePending appends PENDING to fresh mWalPath)
    //   3. Delete backup             (only after all tasks have new WAL entries)
    //
    //   On restart after a crash between steps 1 and 2: mWalPath is missing so the
    //   backup is checked first and tasks are recovered from it.
    //   On restart after a crash during step 2: mWalPath has the partial new entries;
    //   the backup is also read and merged so no task is permanently lost.
    //
    //
    const std::string backupPath = mWalPath + kWalBackupSuffix;

    // Remove stale backup from a previous interrupted recovery, if any.
    //          Crash window note: if the process crashes between this remove and
    //          the rename below, both files are gone. However, pendingMap was already
    //          populated by parseWalFile() above, so the in-memory state is intact
    //          for the current run. The data is only permanently lost if the crash
    //          occurs here AND the process does not recover in this same run.
    //          On first startup (no stale backup) std::remove() is a silent no-op.
    std::remove(backupPath.c_str());

    if (std::rename(mWalPath.c_str(), backupPath.c_str()) != 0)
    {
        // rename() failed (e.g. cross-device) — fall back to delete-first.
        //          This is the pre-fix behaviour; acceptable since rename failure is rare.
        Logger::Warn("WAL: rename to backup failed, falling back to delete-first recovery");
        std::remove(mWalPath.c_str());
    }

    for (auto& [seq, entry] : pendingMap)
    {
        // Re-enqueue recovered task (walSeq=0 so it gets a new WAL entry)
        DBTask task(entry.type, entry.sessionId, entry.data);
        // Note: callback is not recoverable - run without callback
        EnqueueTask(std::move(task));
        ++recoveredCount;
    }

    // All recovered tasks now have fresh WAL entries in mWalPath — safe to drop backup.
    std::remove(backupPath.c_str());

    Logger::Info("WAL: Recovered and re-queued " + std::to_string(recoveredCount) + " task(s)");
}

} // namespace Network::TestServer
