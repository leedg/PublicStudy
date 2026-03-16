#pragma once

// DBServerTaskQueue - async task queue routing client requests through TestDBServer
// 한글: DBServerTaskQueue - 클라이언트 요청을 TestDBServer를 통해 처리하는 비동기 작업 큐
//
// Thread model:
//   - EnqueueTask()   : called from any thread (LogicWorker / IO callbacks)
//   - OnDBResponse()  : called from DBRecvThread
//   - WorkerThread[N] : single thread per worker, exclusively owns sessions[N]
//
// Routing:
//   task   → worker[sessionId % workerCount]  (key-affinity, per-session FIFO)
//   response → worker[KeyGenerator::GetSlot(requestId)] (slot field in 64-bit KeyId)

#include "Utils/KeyGenerator.h"
#include "Interfaces/ResultCode.h"
#include "Network/Core/ServerPacketDefine.h"
#include "Utils/NetworkUtils.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Network::TestServer
{
    using Utils::ConnectionId;

    // =========================================================================
    // Task types
    // 한글: 작업 타입
    // =========================================================================

    enum class DBServerTaskType : uint8_t
    {
        SavePlayerProgress = 0,   // 플레이어 진행도 저장
        LoadPlayerData     = 1,   // 플레이어 데이터 조회
    };

    // =========================================================================
    // DBServerTask — submitted by callers
    // 한글: 호출자가 제출하는 작업 단위
    // =========================================================================

    struct DBServerTask
    {
        DBServerTaskType  type      = DBServerTaskType::SavePlayerProgress;
        ConnectionId      sessionId = 0;
        std::string       data;         // JSON payload

        // Optional custom check lambda (runs after internal type-based check).
        // 한글: 선택적 커스텀 검증 람다 (타입별 내부 검증 이후 실행).
        std::function<bool(const DBServerTask&)>             checkFunc;

        // Result callback — invoked on success, failure, or shutdown.
        // 한글: 성공/실패/종료 시 호출되는 콜백. callback 내부에서 동일 세션으로
        //       EnqueueTask 재호출 금지 (pending 순서 역전 방지).
        std::function<void(ResultCode, const std::string&)>  callback;
    };

    // =========================================================================
    // DBServerTaskQueue
    // =========================================================================

    class DBServerTaskQueue
    {
    public:
        DBServerTaskQueue();
        ~DBServerTaskQueue();

        // Lifecycle
        // 한글: 생명주기
        //
        // workerCount : number of worker threads (max 255 — enforced by if-guard in Initialize)
        // sendFunc    : injected send function (TestServer::SendDBPacket)
        bool Initialize(size_t workerCount,
                        std::function<bool(const void*, uint32_t)> sendFunc);
        void Shutdown();
        bool IsRunning() const;

        // Submit task (non-blocking, move semantics)
        // 한글: 작업 제출 (논블로킹, 이동 의미론)
        void EnqueueTask(DBServerTask&& task);

        // Called by DBServerPacketHandler when PKT_DBQueryRes arrives.
        // 한글: PKT_DBQueryRes 도착 시 DBServerPacketHandler에서 호출.
        void OnDBResponse(uint64_t requestId, ResultCode result,
                          const std::string& detail);

        // Convenience methods
        // 한글: 편의 메서드
        void SavePlayerProgress(ConnectionId sessionId,
                                const std::string& jsonData,
                                std::function<void(ResultCode, const std::string&)> callback);

        size_t GetPendingCount() const;

    private:
        // =====================================================================
        // Internal response event — DBRecvThread → worker queue
        // 한글: DBRecvThread → 워커 큐로 전달되는 내부 응답 이벤트
        // =====================================================================
        struct DBResponseEvent
        {
            uint64_t    requestId = 0;      // KeyGenerator::KeyId (was uint32_t)
            ResultCode  result    = ResultCode::Unknown;
            std::string detail;
        };

        // =====================================================================
        // Per-session in-flight state
        // 한글: 세션별 in-flight 상태 (requestId != 0 이면 in-flight)
        // =====================================================================
        struct SessionState
        {
            uint64_t  requestId = 0;   // 0 = idle (KeyGenerator::kInvalid); was uint32_t
            std::function<void(ResultCode, const std::string&)> callback;
            std::queue<DBServerTask> pending;
        };

        // =====================================================================
        // Per-worker data
        //   Shared (mutex):  taskQueue, responseQueue
        //   Worker-exclusive (no mutex): sessions, seqCounter, index
        // =====================================================================
        struct WorkerData
        {
            explicit WorkerData(size_t idx)
                : keyGen(Utils::KeyTag::DBQuery, static_cast<uint8_t>(idx))
                , index(idx)
            {}

            // Shared — protected by mutex
            std::queue<DBServerTask>    taskQueue;
            std::queue<DBResponseEvent> responseQueue;
            mutable std::mutex          mutex;
            std::condition_variable     cv;
            std::thread                 thread;

            // Worker-exclusive — only touched by this worker's thread
            std::unordered_map<ConnectionId, SessionState> sessions;
            Utils::KeyGenerator         keyGen;  // replaces seqCounter; tag=DBQuery,slot=index
            size_t                      index;   // worker index (for logging)
        };

        // Worker thread entry point
        void WorkerThreadFunc(size_t workerIndex);

        // Process a task (check → send or defer).
        // 한글: 태스크 처리: 검증 → send 또는 pending 보관.
        void ProcessTask(size_t workerIndex, DBServerTask task);

        // Handle a response event (callback → inline next pending).
        // 한글: 응답 처리: callback 호출 → 다음 pending 1개 inline 처리.
        void HandleResponse(size_t workerIndex, const DBResponseEvent& resp);

        // Drain remaining sessions/queues on shutdown (called after thread join).
        // 한글: 종료 시 남은 세션/큐 드레인 (스레드 join 후 호출).
        void ShutdownDrain(size_t workerIndex);

        // Internal type-based check (B approach; extended by task.checkFunc).
        // 한글: 타입별 내부 검증 (B 방식; task.checkFunc으로 확장 가능).
        bool CheckTask(const DBServerTask& task);

    private:
        std::vector<std::unique_ptr<WorkerData>> mWorkers;
        std::atomic<bool>   mIsRunning{false};
        std::atomic<size_t> mPendingCount{0};

        // Injected send function — TestServer::SendDBPacket
        // 한글: 주입된 send 함수 — TestServer::SendDBPacket
        std::function<bool(const void*, uint32_t)> mSendFunc;
    };

} // namespace Network::TestServer
