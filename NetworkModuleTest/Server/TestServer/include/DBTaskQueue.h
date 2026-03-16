#pragma once

// Asynchronous DB task queue - separates game logic from database operations
// 한글: 비동기 DB 작업 큐 - 게임 로직과 데이터베이스 작업 분리

// Forward-declare IDatabase to avoid pulling in ServerEngine headers here
// 한글: ServerEngine 헤더 전이 방지를 위한 IDatabase 전방 선언
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
    // 한글: DB 작업 타입
    // =============================================================================

    enum class DBTaskType : uint8_t
    {
        RecordConnectTime,      // 접속 시간 기록
        RecordDisconnectTime,   // 접속 종료 시간 기록
        UpdatePlayerData,       // 플레이어 데이터 업데이트
    };

    // =============================================================================
    // DB task data
    // 한글: DB 작업 데이터
    // =============================================================================

    struct DBTask
    {
        DBTaskType type;
        ConnectionId sessionId;
        std::string data;     // JSON 또는 직렬화된 데이터
        std::function<void(bool success, const std::string& result)> callback;  // 선택적 콜백
        uint64_t walSeq = 0;  // WAL sequence (0 = not WAL-tracked, e.g. recovered tasks)
                              // 한글: WAL 시퀀스 번호 (0 = WAL 추적 안 함, 예: 복구된 태스크)

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
    // 한글: 키 친화도 라우팅이 적용된 비동기 DB 작업 큐.
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
    // 한글: 세션별 순서 보장:
    //   각 작업은 worker[sessionId % workerCount]로 라우팅됩니다.
    //   동일 세션은 항상 같은 워커에 배정됩니다.
    //   각 워커는 단일 스레드로 FIFO 처리하므로:
    //     (1) 동일 세션의 작업은 항상 같은 DB Worker에서 실행됩니다.
    //     (2) 같은 세션의 B 작업은 A 작업이 DB에서 완전히 완료된 후에만
    //         처리됩니다 — 워커는 한 번에 하나씩 pop하여 처리합니다.
    // =============================================================================

    class DBTaskQueue
    {
    public:
        DBTaskQueue();
        ~DBTaskQueue();

        // Lifecycle
        // 한글: 생명주기
        bool Initialize(size_t workerThreadCount = 1,
                        const std::string& walPath = "db_tasks.wal",
                        Network::Database::IDatabase* db = nullptr);
        void Shutdown();
        bool IsRunning() const;

        // Task submission (non-blocking, move semantics)
        // 한글: 작업 제출 (논블로킹, 이동 의미론)
        void EnqueueTask(DBTask&& task);

        // Convenience methods for common operations
        // 한글: 일반적인 작업을 위한 편의 메서드
        void RecordConnectTime(ConnectionId sessionId, const std::string& timestamp);
        void RecordDisconnectTime(ConnectionId sessionId, const std::string& timestamp);
        void UpdatePlayerData(ConnectionId sessionId, const std::string& jsonData,
                              std::function<void(bool, const std::string&)> callback = nullptr);

        // Statistics
        // 한글: 통계
        size_t GetQueueSize() const;
        size_t GetProcessedCount() const;
        size_t GetFailedCount() const;

    private:
        // Worker thread function
        // 한글: 워커 스레드 함수
        void WorkerThreadFunc(size_t workerIndex);

        // Process individual task
        // 한글: 개별 작업 처리
        bool ProcessTask(const DBTask& task);

        // Specific task handlers
        // 한글: 특정 작업 핸들러
        bool HandleRecordConnectTime(const DBTask& task, std::string& result);
        bool HandleRecordDisconnectTime(const DBTask& task, std::string& result);
        bool HandleUpdatePlayerData(const DBTask& task, std::string& result);

        // =====================================================================
        // WAL (Write-Ahead Log) for crash recovery
        // 한글: 크래시 복구를 위한 WAL (Write-Ahead Log)
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
        // 한글: WAL 파일이 열려 있지 않으면 엽니다. mWalMutex 하에서 호출해야 함.
        //       파일이 열려 있거나 성공적으로 열렸으면 true 반환.
        bool     EnsureWalOpen();

    private:
        // Per-worker data — each worker owns its queue, mutex, cv, and thread.
        //   Routing: sessionId % workerCount → same session always → same worker.
        //   Within a worker: single thread + FIFO → task B is never dequeued until task A completes.
        // 한글: 워커별 데이터 — 각 워커는 자체 큐, mutex, cv, 스레드를 소유합니다.
        //   라우팅: sessionId % workerCount → 동일 세션은 항상 동일 워커.
        //   워커 내부: 단일 스레드 + FIFO → A가 완료되기 전까지 B는 절대 꺼내지지 않음.
        struct WorkerData
        {
            std::queue<DBTask>      taskQueue;
            mutable std::mutex      mutex;
            std::condition_variable cv;
            std::thread             thread;
        };

        std::vector<std::unique_ptr<WorkerData>> mWorkers;

        // Global queue size counter across all workers (lock-free GetQueueSize)
        // 한글: 전체 워커에 걸친 글로벌 큐 크기 카운터 (lock-free GetQueueSize)
        std::atomic<size_t>             mQueueSize;

        std::atomic<bool>               mIsRunning;

        // Statistics
        // 한글: 통계
        std::atomic<size_t>             mProcessedCount;
        std::atomic<size_t>             mFailedCount;

        // WAL crash-recovery members
        // 한글: WAL 크래시 복구 멤버
        std::string                     mWalPath;       // WAL 파일 경로
        std::ofstream                   mWalFile;       // 추가 전용 스트림
        mutable std::mutex              mWalMutex;      // WAL 파일 쓰기 직렬화
        // Injected database (non-owning); nullptr = log-only mode
        // 한글: 주입된 데이터베이스 (non-owning); nullptr이면 로그만 출력
        Network::Database::IDatabase* mDatabase = nullptr;
    };

} // namespace Network::TestServer
