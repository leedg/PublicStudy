// Asynchronous DB task queue implementation
// 한글: 비동기 DB 작업 큐 구현

#include "../include/DBTaskQueue.h"

#include "Utils/KeyGenerator.h"
#include "Utils/Logger.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

// IDatabase interface always available for runtime injection
// 한글: 런타임 주입을 위해 IDatabase 인터페이스는 항상 포함
#include "../include/TestServerSqlSpec.h"
#include "Database/SqlModuleBootstrap.h"
#include "Database/SqlScriptRunner.h"
#include "Interfaces/IDatabase.h"
#include "Interfaces/IStatement.h"

#ifdef ENABLE_DATABASE_SUPPORT
#include "Database/DatabaseModule.h"
#endif

namespace Network::TestServer
{

using namespace Network::Utils;

namespace
{
    constexpr const char* kSqlModuleName = "TestServer";

    template <typename Binder = std::nullptr_t>
    bool ExecuteModuleScript(Network::Database::IDatabase& database,
                             const char* relativePath,
                             Binder&& binder = nullptr)
    {
        return Network::Database::SqlScriptRunner::Execute(
            database, kSqlModuleName, relativePath, std::forward<Binder>(binder));
    }

    template <typename Binder = std::nullptr_t>
    int ExecuteModuleScriptUpdate(Network::Database::IDatabase& database,
                                  const char* relativePath,
                                  Binder&& binder = nullptr)
    {
        return Network::Database::SqlScriptRunner::ExecuteUpdate(
            database, kSqlModuleName, relativePath, std::forward<Binder>(binder));
    }
}

// =============================================================================
// DBTaskQueue implementation
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
    // 한글: 멀티워커 안전: sessionId % workerCount로 각 세션을 전용 워커에 배정.
    //       워커당 단일 스레드 FIFO로 세션별 순서 및 직렬 DB 처리 보장.

    // Store injected database reference and create tables if DB available
    // 한글: 주입된 데이터베이스 저장 및 DB 사용 가능 시 테이블 생성
    mDatabase = db;

    if (mDatabase != nullptr && mDatabase->IsConnected())
    {
        try
        {
            const bool bootstrapped =
                Network::Database::SqlModuleBootstrap::BootstrapModuleIfNeeded(
                    *mDatabase,
                    Network::TestServer::GetTestServerSqlModuleSpec());
            Logger::Info(bootstrapped
                             ? "DBTaskQueue: initial SQL bootstrap completed"
                             : "DBTaskQueue: SQL manifest verified");
        }
        catch (const std::exception& e)
        {
            Logger::Error("DBTaskQueue bootstrap failed: " + std::string(e.what()));
            return false;
        }
    }

    // Start workers BEFORE WAL recovery so EnqueueTask() accepts recovered tasks
    // 한글: WAL 복구 전에 워커 시작 - EnqueueTask()가 복구된 태스크를 받을 수 있도록
    mIsRunning.store(true);

    // Create per-worker data structures first, then start threads.
    // 한글: 워커별 데이터 구조체를 먼저 생성한 뒤 스레드 시작.
    for (size_t i = 0; i < workerThreadCount; ++i)
    {
        mWorkers.push_back(std::make_unique<WorkerData>());
    }

    for (size_t i = 0; i < mWorkers.size(); ++i)
    {
        mWorkers[i]->thread = std::thread(&DBTaskQueue::WorkerThreadFunc, this, i);
    }

    // Set WAL path and re-enqueue tasks from previous crash (if any)
    // 한글: WAL 경로 설정 및 이전 크래시 미완료 태스크 복구 재인큐
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
    // 한글: 모든 워커 스레드에 중지 신호 전송
    mIsRunning.store(false);

    // Wake up every per-worker CV so threads can exit their wait loop.
    // 한글: 각 워커의 CV를 깨워 wait 루프를 탈출하도록 함.
    for (auto& worker : mWorkers)
    {
        worker->cv.notify_all();
    }

    // Wait for all worker threads to finish.
    // 한글: 모든 워커 스레드가 종료될 때까지 대기.
    for (auto& worker : mWorkers)
    {
        if (worker->thread.joinable())
        {
            worker->thread.join();
        }
    }

    // Drain remaining tasks from all per-worker queues.
    //          All workers joined; drain without lock.
    // 한글: 모든 워커별 큐에 남은 작업을 처리합니다.
    //       모든 워커 join 완료 — 락 없이 drain.
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
        // 한글: 수집된 작업을 락 밖에서 실행.
        for (auto& task : drainTasks)
        {
            try
            {
                const bool success = ProcessTask(task);

                // Keep WAL semantics identical to worker path.
                // 한글: 워커 경로와 동일한 WAL 의미 유지.
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
    // 한글: WAL - 큐에 넣기 전에 대기 태스크 기록 (크래시 안전)
    if (task.walSeq == 0) // 새로운/복구 태스크 모두 신규 WAL 시퀀스로 기록
    {
        task.walSeq = WalNextSeq();
        WalWritePending(task, task.walSeq);
    }

    // Key-affinity routing: sessionId % workerCount.
    //          Same session → same worker → FIFO serial processing guaranteed.
    // 한글: 키 친화도 라우팅: sessionId % workerCount.
    //       동일 세션 → 동일 워커 → FIFO 직렬 처리 보장.
    const size_t workerIndex = static_cast<size_t>(task.sessionId) % mWorkers.size();
    WorkerData& worker = *mWorkers[workerIndex];

    bool     accepted        = false;
    bool     shouldWriteDone = false;
    uint64_t canceledWalSeq  = 0;

    {
        std::lock_guard<std::mutex> lock(worker.mutex);

        // Re-check under queue lock to close the Shutdown() race window.
        // 한글: Shutdown()와 경쟁하는 구간을 차단하기 위해 락 안에서 재검증
        if (mIsRunning.load(std::memory_order_acquire))
        {
            worker.taskQueue.push(std::move(task));

            // Increment global queue size counter (lock-free GetQueueSize)
            // 한글: 글로벌 큐 크기 카운터 증가 (lock-free GetQueueSize 가능)
            mQueueSize.fetch_add(1, std::memory_order_relaxed);
            accepted = true;
        }
        else if (task.walSeq != 0)
        {
            // Task was WAL-pended but never queued. Mark done to avoid replay.
            // 한글: WAL PENDING만 기록되고 큐에는 못 들어간 태스크의 재생 방지
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
    // 한글: task 필드 접근 전에 accepted 확인 필수.
    //       락 내부의 push(std::move(task))로 워커 큐에 이동되었으므로
    //       accepted 경로에서 task는 moved-from 상태.
    //       task.walSeq / task.callback 접근은 이동이 발생하지 않은
    //       거부 경로(accepted == false)에서만 안전.
    //       참고: 원래 코드는 조건이 역전(!accepted가 성공 경로를 가드)되어
    //       에러 시 notify_one이 호출되는 버그가 있었음. 이 코드가 수정본.
    if (accepted)
    {
        worker.cv.notify_one();
        return;
    }

    // Reached only in the rejected path — task was NOT moved, so
    //          task.walSeq and task.callback are still valid to access.
    // 한글: 거부 경로에서만 도달 — task가 이동되지 않았으므로
    //       task.walSeq, task.callback 접근 안전.
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
    // 한글: 이동 의미론을 사용한 논블로킹 큐잉
    EnqueueTask(DBTask(DBTaskType::RecordConnectTime, sessionId, timestamp));
    Logger::Debug("Enqueued RecordConnectTime task for Session: " +
                  std::to_string(sessionId));
}

void DBTaskQueue::RecordDisconnectTime(ConnectionId sessionId, const std::string& timestamp)
{
    // Move temporary DBTask object (avoid copy)
    // 한글: 임시 DBTask 객체를 이동 (복사 방지)
    EnqueueTask(DBTask(DBTaskType::RecordDisconnectTime, sessionId, timestamp));
    Logger::Debug("Enqueued RecordDisconnectTime task for Session: " +
                  std::to_string(sessionId));
}

void DBTaskQueue::UpdatePlayerData(ConnectionId sessionId, const std::string& jsonData,
                                   std::function<void(bool, const std::string&)> callback)
{
    // Move temporary DBTask object with callback
    // 한글: 콜백과 함께 임시 DBTask 객체 이동
    EnqueueTask(DBTask(DBTaskType::UpdatePlayerData, sessionId, jsonData, callback));
    Logger::Debug("Enqueued UpdatePlayerData task for Session: " +
                  std::to_string(sessionId));
}

size_t DBTaskQueue::GetQueueSize() const
{
    // Lock-free queue size query (optimization)
    // 한글: Lock-free 큐 크기 조회 (최적화)
    // Performance: Atomic load is ~10-100x faster than mutex acquisition
    // 성능: Atomic load는 mutex 획득보다 약 10-100배 빠름
    // Note: May be slightly inaccurate due to concurrent operations, but acceptable for statistics
    // 참고: 동시 작업으로 인해 약간 부정확할 수 있지만 통계용으로는 충분함
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
        // 한글: 이 워커 전용 CV에서 대기 — 전역 공유 CV 사용 안 함.
        //       sessionId % workerCount == workerIndex인 작업이 들어올 때만
        //       이 워커가 깨어납니다.
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
                // 한글: 글로벌 큐 크기 카운터 감소.
                mQueueSize.fetch_sub(1, std::memory_order_relaxed);
                hasTask = true;
            }
        }

        // Process task outside of lock — DB I/O is blocking but does
        //          not hold the queue lock, so new tasks can be enqueued freely
        //          while this task is executing. The next task for the same
        //          session will not be dequeued until this one returns.
        // 한글: 락 밖에서 작업 처리 — DB I/O는 블로킹이지만 큐 락을 잡지 않으므로
        //       처리 중에도 새 작업을 자유롭게 enqueue할 수 있습니다.
        //       동일 세션의 다음 작업은 이 작업이 반환될 때까지 꺼내지지 않습니다.
        if (hasTask)
        {
            const bool success = ProcessTask(task);

            // WAL - mark task as done after successful processing.
            // 한글: WAL - 처리 완료 후 태스크 완료 마킹.
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
    // 한글: 콜백이 제공된 경우 호출
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
    // 한글: null이면 DB 자체가 설정되지 않은 하드 오류 (로그 모드 아님).
    //       IsConnected() == false는 설정됐지만 일시적으로 오프라인 — 허용, 아래 로그 경로로 진행.
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
            ExecuteModuleScriptUpdate(
                *mDatabase,
                "SP/SP_InsertSessionConnectLog.sql",
                [&](Network::Database::IStatement& stmt)
                {
                    stmt.BindParameter(1, static_cast<long long>(task.sessionId));
                    stmt.BindParameter(2, task.data);
                });
            Logger::Info("DB INSERT T_SessionConnectLog - Session: " +
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
    // 한글: Fallback — DB 미연결 시 로그만 출력
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
            ExecuteModuleScriptUpdate(
                *mDatabase,
                "SP/SP_InsertSessionDisconnectLog.sql",
                [&](Network::Database::IStatement& stmt)
                {
                    stmt.BindParameter(1, static_cast<long long>(task.sessionId));
                    stmt.BindParameter(2, task.data);
                });
            Logger::Info("DB INSERT T_SessionDisconnectLog - Session: " +
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

            // Upsert — insert or replace player data
            // 한글: Upsert — 플레이어 데이터 삽입 또는 교체
            ExecuteModuleScriptUpdate(
                *mDatabase,
                "SP/SP_UpsertPlayerData.sql",
                [&](Network::Database::IStatement& stmt)
                {
                    stmt.BindParameter(1, static_cast<long long>(task.sessionId));
                    stmt.BindParameter(2, task.data);
                });
            Logger::Info("DB UPSERT T_PlayerData - Session: " + std::to_string(task.sessionId));
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
// 한글: WAL 크래시 복구 구현
//
// Format per line: <STATUS>|<TYPE>|<SESSIONID>|<SEQ>|<DATA>
//   P = Pending (written before enqueue)
//   D = Done    (written after successful ProcessTask)
// =============================================================================

uint64_t DBTaskQueue::WalNextSeq()
{
    // KeyGenerator::NextGlobalId() — shared monotonic counter, lock-free.
    // 한글: KeyGenerator 전역 단조 증가 — WAL 시퀀스 번호 발급.
    return Utils::KeyGenerator::NextGlobalId();
}

bool DBTaskQueue::EnsureWalOpen()
{
    // Caller MUST hold mWalMutex before calling this function.
    // 한글: 이 함수를 호출하기 전에 mWalMutex를 보유하고 있어야 합니다.
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
    // 한글: mWalFile의 lazy-open은 mWalMutex로 보호되어 경합 방지.
    std::lock_guard<std::mutex> lock(mWalMutex);
    if (!EnsureWalOpen())
    {
        Logger::Warn("WAL: Failed to open WAL file: " + mWalPath);
        return;
    }

    // Escape '|' in data field to avoid parse ambiguity
    // 한글: 데이터 필드의 '|' 이스케이프 처리 (파싱 모호성 방지)
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
    // 한글: mWalFile의 lazy-open은 mWalMutex로 보호되어 경합 방지.
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
    // 한글: 백업 파일 접미사 — 여기서 한 번 정의하고 읽기 단계(parseWalFile)와
    //       쓰기 단계(rename + 복구) 모두에서 사용.
    const std::string kWalBackupSuffix = ".bak";

    // WAL entry struct (used for both primary and backup file parsing).
    // 한글: 기본 WAL 및 백업 파일 파싱에 모두 사용되는 WAL 항목 구조체.
    struct WalEntry
    {
        DBTaskType   type;
        ConnectionId sessionId;
        std::string  data;
    };

    // Keep pending tasks ordered by sequence for deterministic replay.
    // 한글: 복구 재실행 순서를 고정하기 위해 시퀀스 순 정렬 유지.
    std::map<uint64_t, WalEntry> pendingMap;
    std::string                  line;
    size_t                       recoveredCount = 0;
    uint64_t                     maxSeenSeq     = 0;

    // Helper lambda — parse one WAL file (primary or .bak) into pendingMap.
    //          Called for both files so that a crash during rename-and-re-enqueue does
    //          not permanently lose tasks that only appear in the backup.
    // 한글: 단일 WAL 파일(기본 또는 .bak)을 pendingMap으로 파싱하는 헬퍼 람다.
    //       rename+재인큐 도중 크래시 시 백업 파일에만 남은 태스크도 손실 없이 복구.
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
                // 한글: 동일 seq가 양 파일에 존재하면 높은 seq(신규 파일) 우선.
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
    // 한글: 어떤 크래시 시나리오에서도 태스크 손실이 없도록 기본 WAL과 백업 모두 읽기.
    parseWalFile(mWalPath);
    parseWalFile(mWalPath + kWalBackupSuffix);

    if (pendingMap.empty() && maxSeenSeq == 0)
    {
        // No WAL file found at all = clean startup
        // 한글: WAL 파일 없음 = 정상 시작
        return;
    }

    if (pendingMap.empty())
    {
        // WAL existed but all tasks were completed — remove it (and backup if any).
        // 한글: WAL은 있었지만 모든 태스크 완료 — 기본 및 백업 파일 정리.
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
    // 한글: WAL 크래시 안전 복구 순서:
    //   1. 기존 WAL → 백업 파일로 원자적 rename (복구 중 크래시 시 데이터 보존)
    //   2. 각 태스크 재인큐 (WalWritePending이 새 mWalPath에 PENDING 추가)
    //   3. 백업 파일 삭제 (모든 태스크에 새 WAL 항목이 생긴 후)
    //
    //   1~2 사이 크래시 후 재시작: mWalPath 없음 → 백업 파일에서 복구.
    //   2 도중 크래시 후 재시작: mWalPath에 부분 새 항목 + 백업 파일 병합으로 손실 없음.
    const std::string backupPath = mWalPath + kWalBackupSuffix;

    // Remove stale backup from a previous interrupted recovery, if any.
    //          Crash window note: if the process crashes between this remove and
    //          the rename below, both files are gone. However, pendingMap was already
    //          populated by parseWalFile() above, so the in-memory state is intact
    //          for the current run. The data is only permanently lost if the crash
    //          occurs here AND the process does not recover in this same run.
    //          On first startup (no stale backup) std::remove() is a silent no-op.
    // 한글: 이전 복구가 중단되어 남은 스테일 백업 파일 제거.
    //       크래시 윈도우 주의: 이 remove와 아래 rename 사이에 크래시 발생 시
    //       두 파일 모두 사라짐. 단, pendingMap은 이미 위의 parseWalFile()에서
    //       채워졌으므로 현재 실행에서의 in-memory 상태는 유효.
    //       데이터 영구 손실은 이 지점에서 크래시 + 동일 실행에서 복구 불가 시에만 발생.
    //       최초 시작 시(스테일 백업 없음) std::remove()는 조용한 no-op.
    std::remove(backupPath.c_str());

    if (std::rename(mWalPath.c_str(), backupPath.c_str()) != 0)
    {
        // rename() failed (e.g. cross-device) — fall back to delete-first.
        //          This is the pre-fix behaviour; acceptable since rename failure is rare.
        // 한글: rename() 실패(예: 크로스 디바이스) — 기존 삭제 우선 방식으로 폴백.
        Logger::Warn("WAL: rename to backup failed, falling back to delete-first recovery");
        std::remove(mWalPath.c_str());
    }

    for (auto& [seq, entry] : pendingMap)
    {
        // Re-enqueue recovered task (walSeq=0 so it gets a new WAL entry)
        // 한글: 복구된 태스크 재인큐 (walSeq=0이면 새 WAL 항목 생성)
        DBTask task(entry.type, entry.sessionId, entry.data);
        // Note: callback is not recoverable - run without callback
        EnqueueTask(std::move(task));
        ++recoveredCount;
    }

    // All recovered tasks now have fresh WAL entries in mWalPath — safe to drop backup.
    // 한글: 복구된 태스크 전부 새 WAL 항목 보유 확인 후 백업 삭제.
    std::remove(backupPath.c_str());

    Logger::Info("WAL: Recovered and re-queued " + std::to_string(recoveredCount) + " task(s)");
}

} // namespace Network::TestServer
