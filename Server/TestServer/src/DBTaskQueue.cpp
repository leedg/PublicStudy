// English: Asynchronous DB task queue implementation

// 한글: 비동기 DB 작업 큐 구현



#include "../include/DBTaskQueue.h"

#include "Utils/Logger.h"

#include <chrono>

#include <cstdio>

#include <fstream>

#include <map>

#include <sstream>

#include <vector>



// English: IDatabase interface always available for runtime injection

// 한글: 런타임 주입을 위해 IDatabase 인터페이스는 항상 포함

#include "Interfaces/IDatabase.h"

#include "Interfaces/IStatement.h"



#ifdef ENABLE_DATABASE_SUPPORT

#include "Database/DatabaseModule.h"

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



    bool DBTaskQueue::Initialize(size_t workerThreadCount,

                                 const std::string& walPath,

                                 Network::Database::IDatabase* db)

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



        // English: Store injected database reference and create tables if DB available

        // 한글: 주입된 데이터베이스 저장 및 DB 사용 가능 시 테이블 생성

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

                    Logger::Warn("DBTaskQueue: Failed to create table: " + std::string(e.what()));

                }

            }

            Logger::Info("DBTaskQueue: DB tables ensured (SessionConnectLog, SessionDisconnectLog, PlayerData)");

        }



        // English: Start workers BEFORE WAL recovery so EnqueueTask() accepts recovered tasks

        // 한글: WAL 복구 전에 워커 시작 - EnqueueTask()가 복구된 태스크를 받을 수 있도록

        mIsRunning.store(true);



        try
        {
            for (size_t i = 0; i < workerThreadCount; ++i)
            {
                mWorkerThreads.emplace_back(&DBTaskQueue::WorkerThreadFunc, this);
            }
        }
        catch (const std::exception& e)
        {
            Logger::Warn("Failed to create worker thread: " + std::string(e.what()));
            mIsRunning.store(false);
            for (auto& t : mWorkerThreads)
            {
                if (t.joinable())
                {
                    t.join();
                }
            }
            mWorkerThreads.clear();
            return false;
        }
        }



        // English: Set WAL path and re-enqueue tasks from previous crash (if any)

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

                    const bool success = ProcessTask(task);



                    // English: Keep WAL semantics identical to worker path:

                    //          once processed during drain, mark as done.

                    // 한글: 워커 경로와 동일한 WAL 의미를 유지:

                    //       drain에서 처리된 태스크도 done 마킹.

                    if (success && task.walSeq != 0)

                    {

                        WalWriteDone(task.walSeq);

                    }

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

        if (!mIsRunning.load(std::memory_order_acquire))

        {

            Logger::Error("Cannot enqueue task - DBTaskQueue not running");

            if (task.callback)

            {

                task.callback(false, "DBTaskQueue not running");

            }

            return;

        }



        // English: WAL - record pending task before queueing (crash-safe)

        // 한글: WAL - 큐에 넣기 전에 대기 태스크 기록 (크래시 안전)

        if (task.walSeq == 0)  // 새로운/복구 태스크 모두 신규 WAL 시퀀스로 기록

        {

            task.walSeq = WalNextSeq();

            WalWritePending(task, task.walSeq);

        }



        bool accepted = false;

        bool shouldWriteDone = false;

        uint64_t canceledWalSeq = 0;

        {

            std::lock_guard<std::mutex> lock(mQueueMutex);



            // English: Re-check under queue lock to close the Shutdown() race window.

            // 한글: Shutdown()와 경쟁하는 구간을 차단하기 위해 락 안에서 재검증

            if (mIsRunning.load(std::memory_order_acquire))

            {

                mTaskQueue.push(std::move(task));



                // English: Increment queue size atomically (enables lock-free GetQueueSize)

                // 한글: Atomic으로 큐 크기 증가 (lock-free GetQueueSize 가능)

                mQueueSize.fetch_add(1, std::memory_order_relaxed);

                accepted = true;

            }

            else if (task.walSeq != 0)

            {

                // English: Task was WAL-pended but never queued. Mark done to avoid replay.

                // 한글: WAL PENDING만 기록되고 큐에는 못 들어간 태스크의 재생 방지

                shouldWriteDone = true;

                canceledWalSeq = task.walSeq;

            }

        }



        if (!accepted)

        {

            if (shouldWriteDone)

            {

                WalWriteDone(canceledWalSeq);

            }



            Logger::Error("Cannot enqueue task - DBTaskQueue shutting down");

            if (task.callback)

            {

                task.callback(false, "DBTaskQueue shutting down");

            }

            return;

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

                const bool success = ProcessTask(task);



                // English: WAL - mark task as done after successful processing

                // 한글: WAL - 처리 완료 후 태스크 완료 마킹

                if (success && task.walSeq != 0)

                {

                    WalWriteDone(task.walSeq);

                }

            }

        }



        Logger::Info("DBTaskQueue worker thread stopped");

    }



    bool DBTaskQueue::ProcessTask(const DBTask& task)

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



        return success;

    }



    bool DBTaskQueue::HandleRecordConnectTime(const DBTask& task, std::string& result)

    {

        if (mDatabase == nullptr)

        {

            Logger::Warn("HandleRecordConnectTime: mDatabase is null");

            result = "Database pointer is null";

            return false;

        }



        if (mDatabase != nullptr && mDatabase->IsConnected())

        {

            try

            {

                auto stmt = mDatabase->CreateStatement();

                stmt->SetQuery(

                    "INSERT INTO SessionConnectLog (session_id, connect_time) VALUES (?, ?)");

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



        // English: Fallback — log only (no DB connected)

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



        if (mDatabase != nullptr && mDatabase->IsConnected())

        {

            try

            {

                auto stmt = mDatabase->CreateStatement();

                stmt->SetQuery(

                    "INSERT INTO SessionDisconnectLog (session_id, disconnect_time) VALUES (?, ?)");

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



        if (mDatabase != nullptr && mDatabase->IsConnected())

        {

            try

            {

                auto stmt = mDatabase->CreateStatement();

                // English: Upsert — insert or replace player data

                // 한글: Upsert — 플레이어 데이터 삽입 또는 교체

                stmt->SetQuery(

                    "INSERT OR REPLACE INTO PlayerData (session_id, data) VALUES (?, ?)");

                stmt->BindParameter(1, static_cast<long long>(task.sessionId));

                stmt->BindParameter(2, task.data);

                stmt->ExecuteUpdate();



                Logger::Info("DB UPSERT PlayerData - Session: " +

                             std::to_string(task.sessionId));

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

    // English: WAL (Write-Ahead Log) crash recovery implementation

    // 한글: WAL 크래시 복구 구현

    //

    // Format per line: <STATUS>|<TYPE>|<SESSIONID>|<SEQ>|<DATA>

    //   P = Pending (written before enqueue)

    //   D = Done    (written after successful ProcessTask)

    // =============================================================================



    uint64_t DBTaskQueue::WalNextSeq()

    {

        // English: Monotonically increasing, unique sequence number

        // 한글: 단조 증가 고유 시퀀스 번호

        return mWalSeq.fetch_add(1, std::memory_order_relaxed) + 1;

    }



    void DBTaskQueue::WalWritePending(const DBTask& task, uint64_t seq)

    {

        if (mWalPath.empty())

        {

            return;

        }



        // English: mWalFile lazy-open is protected by mWalMutex to prevent race conditions.

        // 한글: mWalFile의 lazy-open은 mWalMutex로 보호되어 경합 방지.

        std::lock_guard<std::mutex> lock(mWalMutex);



        if (!mWalFile.is_open())

        {

            mWalFile.open(mWalPath, std::ios::app | std::ios::out);

            if (!mWalFile.is_open())

            {

                Logger::Warn("WAL: Failed to open WAL file: " + mWalPath);

                return;

            }

        }



        // English: Escape '|' in data field to avoid parse ambiguity

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



        // English: mWalFile lazy-open is protected by mWalMutex to prevent race conditions.

        // 한글: mWalFile의 lazy-open은 mWalMutex로 보호되어 경합 방지.

        std::lock_guard<std::mutex> lock(mWalMutex);



        if (!mWalFile.is_open())

        {

            mWalFile.open(mWalPath, std::ios::app | std::ios::out);

            if (!mWalFile.is_open())

            {

                return;

            }

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



        // English: WAL entry struct (used for both primary and backup file parsing).

        // 한글: 기본 WAL 및 백업 파일 파싱에 모두 사용되는 WAL 항목 구조체.

        struct WalEntry

        {

            DBTaskType type;

            ConnectionId sessionId;

            std::string data;

        };



        // English: Keep pending tasks ordered by sequence for deterministic replay.

        // 한글: 복구 재실행 순서를 고정하기 위해 시퀀스 순 정렬 유지.

        std::map<uint64_t, WalEntry> pendingMap;

        std::string line;

        size_t recoveredCount = 0;

        uint64_t maxSeenSeq = 0;



        // English: Helper lambda — parse one WAL file (primary or .bak) into pendingMap.

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

                std::string status;

                if (!std::getline(ss, status, '|')) continue;



                if (status == "P")

                {

                    std::string typeStr, sessionStr, seqStr, data;

                    if (!std::getline(ss, typeStr, '|')) continue;

                    if (!std::getline(ss, sessionStr, '|')) continue;

                    if (!std::getline(ss, seqStr, '|')) continue;

                    std::getline(ss, data);



                    uint64_t seq = 0;

                    try { seq = std::stoull(seqStr); } catch (...) { continue; }

                    if (seq > maxSeenSeq) maxSeenSeq = seq;



                    for (auto& c : data) { if (c == '\x01') c = '|'; }



                    int typeInt = 0;

                    try { typeInt = std::stoi(typeStr); } catch (...) { continue; }

                    ConnectionId sessionId = 0;

                    try { sessionId = static_cast<ConnectionId>(std::stoull(sessionStr)); } catch (...) { continue; }



                    WalEntry entry;

                    entry.type  = static_cast<DBTaskType>(typeInt);

                    entry.sessionId = sessionId;

                    entry.data  = data;

                    // English: Higher-seq entry wins if both files have the same seq.

                    // 한글: 동일 seq가 양 파일에 존재하면 높은 seq(신규 파일) 우선.

                    pendingMap.emplace(seq, std::move(entry));

                }

                else if (status == "D")

                {

                    std::string seqStr;

                    if (!std::getline(ss, seqStr, '|')) continue;

                    uint64_t seq = 0;

                    try { seq = std::stoull(seqStr); } catch (...) { continue; }

                    if (seq > maxSeenSeq) maxSeenSeq = seq;

                    pendingMap.erase(seq);

                }

            }

        };



        // English: Read primary WAL and backup (if present) so we never lose tasks

        //          regardless of which crash scenario occurred.

        // 한글: 어떤 크래시 시나리오에서도 태스크 손실이 없도록 기본 WAL과 백업 모두 읽기.

        const std::string kBackupSuffixEarly = ".bak";

        parseWalFile(mWalPath);

        parseWalFile(mWalPath + kBackupSuffixEarly);



        if (pendingMap.empty() && maxSeenSeq == 0)

        {

            // English: No WAL file found at all = clean startup

            // 한글: WAL 파일 없음 = 정상 시작

            return;

        }



        // English: Keep sequence monotonic after restart.

        // 한글: 재시작 후에도 시퀀스 단조 증가 유지.

        mWalSeq.store(maxSeenSeq, std::memory_order_relaxed);



        if (pendingMap.empty())

        {

            // English: WAL existed but all tasks were completed — remove it (and backup if any).

            // 한글: WAL은 있었지만 모든 태스크 완료 — 기본 및 백업 파일 정리.

            std::remove(mWalPath.c_str());

            std::remove((mWalPath + kBackupSuffixEarly).c_str());

            Logger::Info("WAL: Clean startup (no pending tasks to recover)");

            return;

        }



        Logger::Warn("WAL: Recovering " + std::to_string(pendingMap.size()) +

                     " unfinished task(s) from previous crash");



        // English: WAL crash-safe recovery order:

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

        const std::string kBackupSuffix = ".bak";

        const std::string backupPath = mWalPath + kBackupSuffix;



        // English: Remove stale backup from a previous interrupted recovery, if any.

        // 한글: 이전 복구가 중단되어 남은 스테일 백업 파일 제거.

        std::remove(backupPath.c_str());



        if (std::rename(mWalPath.c_str(), backupPath.c_str()) != 0)

        {

            // English: rename() failed (e.g. cross-device) — fall back to delete-first.

            //          This is the pre-fix behaviour; acceptable since rename failure is rare.

            // 한글: rename() 실패(예: 크로스 디바이스) — 기존 삭제 우선 방식으로 폴백.

            Logger::Warn("WAL: rename to backup failed, falling back to delete-first recovery");

            std::remove(mWalPath.c_str());

        }



        for (auto& [seq, entry] : pendingMap)

        {

            // English: Re-enqueue recovered task (walSeq=0 so it gets a new WAL entry)

            // 한글: 복구된 태스크 재인큐 (walSeq=0이면 새 WAL 항목 생성)

            DBTask task(entry.type, entry.sessionId, entry.data);

            // Note: callback is not recoverable - run without callback

            EnqueueTask(std::move(task));

            ++recoveredCount;

        }



        // English: All recovered tasks now have fresh WAL entries in mWalPath — safe to drop backup.

        // 한글: 복구된 태스크 전부 새 WAL 항목 보유 확인 후 백업 삭제.

        std::remove(backupPath.c_str());



        Logger::Info("WAL: Recovered and re-queued " + std::to_string(recoveredCount) + " task(s)");

    }



} // namespace Network::TestServer

