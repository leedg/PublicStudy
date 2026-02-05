#pragma once

// English: Asynchronous DB task queue - separates game logic from database operations
// 한글: 비동기 DB 작업 큐 - 게임 로직과 데이터베이스 작업 분리

#include "Utils/NetworkUtils.h"
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>

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
        std::string data;  // JSON 또는 직렬화된 데이터
        std::function<void(bool success, const std::string& result)> callback;  // 선택적 콜백

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
    // =============================================================================

    class DBTaskQueue
    {
    public:
        DBTaskQueue();
        ~DBTaskQueue();

        // English: Lifecycle
        // 한글: 생명주기
        bool Initialize(size_t workerThreadCount = 1);
        void Shutdown();
        bool IsRunning() const;

        // English: Task submission (non-blocking)
        // 한글: 작업 제출 (논블로킹)
        void EnqueueTask(DBTask task);

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

    private:
        // English: Task queue
        // 한글: 작업 큐
        std::queue<DBTask>              mTaskQueue;
        mutable std::mutex              mQueueMutex;
        std::condition_variable         mQueueCV;

        // English: Worker threads
        // 한글: 워커 스레드
        std::vector<std::thread>        mWorkerThreads;
        std::atomic<bool>               mIsRunning;

        // English: Statistics
        // 한글: 통계
        std::atomic<size_t>             mProcessedCount;
        std::atomic<size_t>             mFailedCount;

        // English: Database connection pool reference (TODO: inject externally)
        // 한글: 데이터베이스 연결 풀 참조 (TODO: 외부에서 주입)
        // Note: For now, we'll manage DB operations internally
        // 참고: 현재는 DB 작업을 내부에서 관리
    };

} // namespace Network::TestServer
