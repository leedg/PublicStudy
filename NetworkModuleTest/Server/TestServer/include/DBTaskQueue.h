#pragma once

// English: Asynchronous DB task queue - separates game logic from database operations
// 한글: 비동기 DB 작업 큐 - 게임 로직과 데이터베이스 작업 분리

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
    // English: DB task types
    // 한글: DB 작업 타입
    // =============================================================================

    enum class DBTaskType
    {
        RecordConnectTime,      // 접속 시간 기록
        RecordDisconnectTime,   // 접속 종료 시간 기록
        UpdatePlayerData,       // 플레이어 데이터 업데이트
        SaveGameProgress,       // 게임 진행 상황 저장
        Custom                  // 커스텀 쿼리
    };

    // =============================================================================
    // English: DB task data
    // 한글: DB 작업 데이터
    // =============================================================================

    struct DBTask
    {
        DBTaskType type;
        ConnectionId sessionId;
        std::string data;     // JSON 또는 직렬화된 데이터
        std::function<void(bool success, const std::string& result)> callback;  // 선택적 콜백
        uint64_t walSeq = 0;  // English: WAL sequence (0 = not WAL-tracked, e.g. recovered tasks)
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
    // English: Asynchronous DB task queue
    // 한글: 비동기 DB 작업 큐
    //
    // English: WARNING - Multi-worker ordering caveat:
    //   When workerThreadCount > 1, tasks for the same sessionId may execute
    //   out of order because multiple workers dequeue from a single shared queue.
    //   If per-session ordering is required, use workerThreadCount = 1, or
    //   migrate to OrderedTaskQueue (hash-based thread affinity) pattern.
    //
    // 한글: 경고 - 멀티워커 순서 주의:
    //   workerThreadCount > 1인 경우, 같은 sessionId의 작업이 순서가 보장되지 않을 수 있음.
    //   여러 워커가 하나의 공유 큐에서 가져가므로 동일 세션의 작업이 서로 다른 워커에서
    //   동시에 실행될 수 있음. 세션별 순서가 필요하면 workerThreadCount = 1을 사용하거나,
    //   OrderedTaskQueue (해시 기반 스레드 친화도) 패턴으로 전환 필요.
    // =============================================================================

    class DBTaskQueue
    {
    public:
        DBTaskQueue();
        ~DBTaskQueue();

        // English: Lifecycle
        // 한글: 생명주기
        bool Initialize(size_t workerThreadCount = 1,
                        const std::string& walPath = "db_tasks.wal");
        void Shutdown();
        bool IsRunning() const;

        // English: Task submission (non-blocking, move semantics)
        // 한글: 작업 제출 (논블로킹, 이동 의미론)
        void EnqueueTask(DBTask&& task);

        // English: Convenience methods for common operations
        // 한글: 일반적인 작업을 위한 편의 메서드
        void RecordConnectTime(ConnectionId sessionId, const std::string& timestamp);
        void RecordDisconnectTime(ConnectionId sessionId, const std::string& timestamp);
        void UpdatePlayerData(ConnectionId sessionId, const std::string& jsonData,
                              std::function<void(bool, const std::string&)> callback = nullptr);

        // English: Statistics
        // 한글: 통계
        size_t GetQueueSize() const;
        size_t GetProcessedCount() const;
        size_t GetFailedCount() const;

    private:
        // English: Worker thread function
        // 한글: 워커 스레드 함수
        void WorkerThreadFunc();

        // English: Process individual task
        // 한글: 개별 작업 처리
        void ProcessTask(const DBTask& task);

        // English: Specific task handlers
        // 한글: 특정 작업 핸들러
        bool HandleRecordConnectTime(const DBTask& task, std::string& result);
        bool HandleRecordDisconnectTime(const DBTask& task, std::string& result);
        bool HandleUpdatePlayerData(const DBTask& task, std::string& result);

        // =====================================================================
        // English: WAL (Write-Ahead Log) for crash recovery
        // 한글: 크래시 복구를 위한 WAL (Write-Ahead Log)
        //
        // Format per line: <STATUS>|<TYPE>|<SESSIONID>|<SEQ>|<DATA>
        //   STATUS: P(Pending) or D(Done)
        //   TYPE: DBTaskType as integer
        //   SEQ: monotonic sequence number for matching P/D pairs
        // =====================================================================
        void     WalWritePending(const DBTask& task, uint64_t seq);
        void     WalWriteDone(uint64_t seq);
        void     WalRecover();
        uint64_t WalNextSeq();

    private:
        // English: Task queue with lock contention optimization
        // 한글: Lock 경합 최적화가 적용된 작업 큐
        std::queue<DBTask>              mTaskQueue;
        mutable std::mutex              mQueueMutex;
        std::condition_variable         mQueueCV;

        // English: Lock-free queue size counter (optimization for GetQueueSize)
        // 한글: Lock-free 큐 크기 카운터 (GetQueueSize 최적화)
        std::atomic<size_t>             mQueueSize;

        // English: Worker threads
        // 한글: 워커 스레드
        std::vector<std::thread>        mWorkerThreads;
        std::atomic<bool>               mIsRunning;

        // English: Statistics
        // 한글: 통계
        std::atomic<size_t>             mProcessedCount;
        std::atomic<size_t>             mFailedCount;

        // English: WAL crash-recovery members
        // 한글: WAL 크래시 복구 멤버
        std::string                     mWalPath;       // WAL 파일 경로
        std::ofstream                   mWalFile;       // 추가 전용 스트림
        mutable std::mutex              mWalMutex;      // WAL 파일 쓰기 직렬화
        std::atomic<uint64_t>           mWalSeq{0};     // 단조 증가 시퀀스 번호

        // English: Database connection pool reference (TODO: inject externally)
        // 한글: 데이터베이스 연결 풀 참조 (TODO: 외부에서 주입)
        // Note: For now, we'll manage DB operations internally
        // 참고: 현재는 DB 작업을 내부에서 관리
    };

} // namespace Network::TestServer
