# DBServerTaskQueue Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** TestServer에서 클라이언트 요청을 TestDBServer로 중계하는 비동기 작업 큐(`DBServerTaskQueue`) 구현 — per-session 순서 보장, 뮤텍스 최소화, 글로벌 공유 상태 없음.

**Architecture:** key-affinity 라우팅(sessionId % workerCount)으로 동일 세션은 항상 동일 워커에 배정. 워커 큐(taskQueue + responseQueue)를 단일 mutex+cv로 보호. 세션별 in-flight 상태(`sessions` map)는 워커 전용으로 뮤텍스 불필요. requestId 상위 8비트에 workerIndex를 인코딩해 DBRecvThread가 글로벌 lookup 없이 해당 워커로 응답을 라우팅.

**Tech Stack:** C++17, MSVC/VS2022, WinSock2, MSBuild x64 Debug

**Spec:** `docs/superpowers/specs/2026-03-16-dbserver-task-queue-design.md`

---

## Chunk 1: Foundation

### Task 1: ResultCode.h — 공용 결과 코드 enum

**Files:**
- Create: `Server/ServerEngine/Interfaces/ResultCode.h`

- [ ] **Step 1: 파일 생성**

```cpp
#pragma once

// Shared result code enum — used by both client responses and server-server communication.
// 한글: 공용 결과 코드 enum — 클라이언트 응답과 서버간 통신 모두 사용.

#include <cstdint>

namespace Network
{
    // =========================================================================
    // ResultCode — int32_t 기반으로 확장 가능한 결과 코드
    // =========================================================================

    enum class ResultCode : int32_t
    {
        // ── 성공 ──────────────────────────────────────────────────────────────
        Success                 = 0,

        // ── 공통 오류 (1~999) ─────────────────────────────────────────────────
        Unknown                 = 1,
        InvalidRequest          = 2,
        Timeout                 = 3,
        NotInitialized          = 4,
        ShuttingDown            = 5,

        // ── 세션 / 인증 오류 (1000~1999) ──────────────────────────────────────
        SessionNotFound         = 1000,
        SessionExpired          = 1001,
        NotAuthenticated        = 1002,
        PermissionDenied        = 1003,
        DuplicateSession        = 1004,

        // ── DB 오류 (2000~2999) ───────────────────────────────────────────────
        DBConnectionFailed      = 2000,
        DBQueryFailed           = 2001,
        DBRecordNotFound        = 2002,
        DBDuplicateKey          = 2003,
        DBTransactionFailed     = 2004,

        // ── 서버간 통신 오류 (3000~3999) ──────────────────────────────────────
        DBServerNotConnected    = 3000,
        DBServerTimeout         = 3001,
        DBServerRejected        = 3002,

        // ── 게임 로직 오류 (4000~4999) ────────────────────────────────────────
        InsufficientResources   = 4000,
        InvalidGameState        = 4001,
        ItemNotFound            = 4002,
        LevelRequirementNotMet  = 4003,

        Max
    };

    inline bool IsSuccess(ResultCode code) { return code == ResultCode::Success; }

    inline const char* ToString(ResultCode code)
    {
        switch (code)
        {
        case ResultCode::Success:               return "Success";
        case ResultCode::Unknown:               return "Unknown";
        case ResultCode::InvalidRequest:        return "InvalidRequest";
        case ResultCode::Timeout:               return "Timeout";
        case ResultCode::NotInitialized:        return "NotInitialized";
        case ResultCode::ShuttingDown:          return "ShuttingDown";
        case ResultCode::SessionNotFound:       return "SessionNotFound";
        case ResultCode::SessionExpired:        return "SessionExpired";
        case ResultCode::NotAuthenticated:      return "NotAuthenticated";
        case ResultCode::PermissionDenied:      return "PermissionDenied";
        case ResultCode::DuplicateSession:      return "DuplicateSession";
        case ResultCode::DBConnectionFailed:    return "DBConnectionFailed";
        case ResultCode::DBQueryFailed:         return "DBQueryFailed";
        case ResultCode::DBRecordNotFound:      return "DBRecordNotFound";
        case ResultCode::DBDuplicateKey:        return "DBDuplicateKey";
        case ResultCode::DBTransactionFailed:   return "DBTransactionFailed";
        case ResultCode::DBServerNotConnected:  return "DBServerNotConnected";
        case ResultCode::DBServerTimeout:       return "DBServerTimeout";
        case ResultCode::DBServerRejected:      return "DBServerRejected";
        case ResultCode::InsufficientResources: return "InsufficientResources";
        case ResultCode::InvalidGameState:      return "InvalidGameState";
        case ResultCode::ItemNotFound:          return "ItemNotFound";
        case ResultCode::LevelRequirementNotMet:return "LevelRequirementNotMet";
        default:                                return "Unknown";
        }
    }

} // namespace Network
```

- [ ] **Step 2: ServerEngine.vcxproj — ClInclude 추가**

`Server/ServerEngine/ServerEngine.vcxproj` 의 `<ClInclude Include="Interfaces\DatabaseType_enum.h" />` 줄 위에:
```xml
<ClInclude Include="Interfaces\ResultCode.h" />
```

- [ ] **Step 3: ServerEngine.vcxproj.filters — Interfaces 필터에 추가**

`Server/ServerEngine/ServerEngine.vcxproj.filters` 의 `<!-- Interfaces - DB -->` 섹션 첫 항목 위에:
```xml
<ClInclude Include="Interfaces\ResultCode.h">
  <Filter>Interfaces</Filter>
</ClInclude>
```

- [ ] **Step 4: 커밋**

```bash
git add Server/ServerEngine/Interfaces/ResultCode.h
git add Server/ServerEngine/ServerEngine.vcxproj
git add Server/ServerEngine/ServerEngine.vcxproj.filters
git commit -m "feat: add shared ResultCode enum to ServerEngine/Interfaces"
```

---

### Task 2: ServerPacketDefine.h — PKT_DBQueryReq/Res 재설계

**Files:**
- Modify: `Server/ServerEngine/Network/Core/ServerPacketDefine.h`

기존 `PKT_DBQueryReq`/`PKT_DBQueryRes`를 재설계. `PKT_DBSavePingTimeReq/Res` 경로는 변경 없음.

- [ ] **Step 1: PKT_DBQueryReq 교체**

기존:
```cpp
struct PKT_DBQueryReq {
    ...
    uint32_t queryId;
    uint16_t queryLength;
    char query[512];
    ...
};
```

신규 (동일 위치에서 교체):
```cpp
struct PKT_DBQueryReq
{
    static constexpr ServerPacketType PacketId = ServerPacketType::DBQueryReq;

    ServerPacketHeader header;
    uint32_t  queryId;       // requestId = (workerIndex<<24) | seq
    uint8_t   taskType;      // DBServerTaskType (cast to uint8_t)
    uint16_t  dataLength;    // bytes used in data[] (not including null terminator)
    char      data[512];     // JSON payload (null-terminated, dataLength bytes valid)

    PKT_DBQueryReq() : queryId(0), taskType(0), dataLength(0)
    {
        header.InitPacket<PKT_DBQueryReq>();
        data[0] = '\0';
    }
};
```

- [ ] **Step 2: PKT_DBQueryRes 교체**

기존:
```cpp
struct PKT_DBQueryRes {
    ...
    uint32_t queryId;
    uint8_t result;
    uint16_t dataLength;
    char data[1024];
    ...
};
```

신규 (동일 위치에서 교체):
```cpp
struct PKT_DBQueryRes
{
    static constexpr ServerPacketType PacketId = ServerPacketType::DBQueryRes;

    ServerPacketHeader header;
    uint32_t  queryId;       // requestId echo
    int32_t   result;        // Network::ResultCode cast to int32_t
    uint16_t  detailLength;  // bytes used in detail[]
    char      detail[256];   // detail message (null-terminated)

    PKT_DBQueryRes() : queryId(0), result(0), detailLength(0)
    {
        header.InitPacket<PKT_DBQueryRes>();
        detail[0] = '\0';
    }
};
```

- [ ] **Step 3: 커밋**

```bash
git add Server/ServerEngine/Network/Core/ServerPacketDefine.h
git commit -m "feat: redesign PKT_DBQueryReq/Res — taskType/ResultCode, remove raw SQL fields"
```

---

## Chunk 2: DBServerTaskQueue

### Task 3: DBServerTaskQueue.h — 헤더

**Files:**
- Create: `Server/TestServer/include/DBServerTaskQueue.h`

- [ ] **Step 1: 헤더 작성**

```cpp
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
//   response → worker[(requestId>>24) & 0xFF] (encoded in requestId by worker)

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
        void OnDBResponse(uint32_t requestId, ResultCode result,
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
            uint32_t    requestId = 0;
            ResultCode  result    = ResultCode::Unknown;
            std::string detail;
        };

        // =====================================================================
        // Per-session in-flight state
        // 한글: 세션별 in-flight 상태 (requestId != 0 이면 in-flight)
        // =====================================================================
        struct SessionState
        {
            uint32_t  requestId = 0;   // 0 = idle
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
            // Shared — protected by mutex
            std::queue<DBServerTask>    taskQueue;
            std::queue<DBResponseEvent> responseQueue;
            mutable std::mutex          mutex;
            std::condition_variable     cv;
            std::thread                 thread;

            // Worker-exclusive — only touched by this worker's thread
            std::unordered_map<ConnectionId, SessionState> sessions;
            uint32_t  seqCounter = 0;   // lower 24 bits of requestId
            size_t    index      = 0;   // this worker's index
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

        // Generate next requestId for this worker.
        // 한글: 워커별 다음 requestId 생성.
        uint32_t NextRequestId(WorkerData& worker);

    private:
        std::vector<std::unique_ptr<WorkerData>> mWorkers;
        std::atomic<bool>   mIsRunning{false};
        std::atomic<size_t> mPendingCount{0};

        // Injected send function — TestServer::SendDBPacket
        // 한글: 주입된 send 함수 — TestServer::SendDBPacket
        std::function<bool(const void*, uint32_t)> mSendFunc;
    };

} // namespace Network::TestServer
```

- [ ] **Step 2: 커밋**

```bash
git add Server/TestServer/include/DBServerTaskQueue.h
git commit -m "feat: add DBServerTaskQueue header"
```

---

### Task 4: DBServerTaskQueue.cpp — 구현

**Files:**
- Create: `Server/TestServer/src/DBServerTaskQueue.cpp`

- [ ] **Step 1: 구현 작성**

```cpp
// DBServerTaskQueue implementation
// 한글: DBServerTaskQueue 구현

#include "../include/DBServerTaskQueue.h"
#include "Utils/Logger.h"

#include <cstring>

namespace Network::TestServer
{

using namespace Network::Utils;

// =============================================================================
// Constructor / Destructor
// =============================================================================

DBServerTaskQueue::DBServerTaskQueue() = default;

DBServerTaskQueue::~DBServerTaskQueue()
{
    if (mIsRunning.load())
    {
        Shutdown();
    }
}

// =============================================================================
// Lifecycle
// =============================================================================

bool DBServerTaskQueue::Initialize(size_t workerCount,
                                   std::function<bool(const void*, uint32_t)> sendFunc)
{
    if (mIsRunning.load())
    {
        Logger::Warn("DBServerTaskQueue already running");
        return true;
    }

    // workerIndex is encoded in upper 8 bits of requestId → max 255 workers
    // 한글: requestId 상위 8비트에 workerIndex 인코딩 → 최대 255개 워커
    if (workerCount == 0 || workerCount > 255)
    {
        Logger::Error("DBServerTaskQueue: workerCount must be in [1, 255], got " +
                      std::to_string(workerCount));
        return false;
    }

    if (!sendFunc)
    {
        Logger::Error("DBServerTaskQueue: sendFunc must not be null");
        return false;
    }

    mSendFunc = std::move(sendFunc);
    mIsRunning.store(true);

    for (size_t i = 0; i < workerCount; ++i)
    {
        auto wd    = std::make_unique<WorkerData>();
        wd->index  = i;
        mWorkers.push_back(std::move(wd));
    }

    for (size_t i = 0; i < mWorkers.size(); ++i)
    {
        mWorkers[i]->thread = std::thread(&DBServerTaskQueue::WorkerThreadFunc, this, i);
    }

    Logger::Info("DBServerTaskQueue initialized with " +
                 std::to_string(workerCount) + " worker(s)");
    return true;
}

void DBServerTaskQueue::Shutdown()
{
    if (!mIsRunning.load())
    {
        return;
    }

    Logger::Info("DBServerTaskQueue shutting down...");

    mIsRunning.store(false);

    // Wake all workers so they exit their wait loop.
    // 한글: 모든 워커의 wait 루프를 깨움.
    for (auto& w : mWorkers)
    {
        w->cv.notify_all();
    }

    // Join all worker threads.
    // 한글: 모든 워커 스레드 join.
    for (auto& w : mWorkers)
    {
        if (w->thread.joinable())
        {
            w->thread.join();
        }
    }

    // Drain remaining items after threads are joined (no concurrent access).
    // 한글: 스레드 join 완료 후 남은 항목 드레인 (동시 접근 없음).
    for (size_t i = 0; i < mWorkers.size(); ++i)
    {
        ShutdownDrain(i);
    }

    Logger::Info("DBServerTaskQueue shutdown complete");
}

bool DBServerTaskQueue::IsRunning() const
{
    return mIsRunning.load();
}

// =============================================================================
// EnqueueTask — called from any thread
// 한글: 임의 스레드에서 호출
// =============================================================================

void DBServerTaskQueue::EnqueueTask(DBServerTask&& task)
{
    if (!mIsRunning.load(std::memory_order_acquire))
    {
        Logger::Error("DBServerTaskQueue: EnqueueTask called while not running");
        if (task.callback)
        {
            task.callback(ResultCode::ShuttingDown, "DBServerTaskQueue not running");
        }
        return;
    }

    if (mWorkers.empty())
    {
        Logger::Error("DBServerTaskQueue: no workers");
        if (task.callback)
        {
            task.callback(ResultCode::NotInitialized, "No workers");
        }
        return;
    }

    const size_t workerIndex =
        static_cast<size_t>(task.sessionId) % mWorkers.size();
    WorkerData& worker = *mWorkers[workerIndex];

    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(worker.mutex);

        // Double-check under lock to close shutdown race window.
        // 한글: 락 내부에서 재확인 — Shutdown과 경합 구간 차단.
        if (mIsRunning.load(std::memory_order_acquire))
        {
            mPendingCount.fetch_add(1, std::memory_order_relaxed);
            worker.taskQueue.push(std::move(task));
            accepted = true;
        }
    }

    if (accepted)
    {
        worker.cv.notify_one();
    }
    else
    {
        Logger::Error("DBServerTaskQueue: EnqueueTask rejected (shutting down)");
        if (task.callback)
        {
            task.callback(ResultCode::ShuttingDown, "DBServerTaskQueue shutting down");
        }
    }
}

// =============================================================================
// OnDBResponse — called from DBRecvThread
// 한글: DBRecvThread에서 호출
// =============================================================================

void DBServerTaskQueue::OnDBResponse(uint32_t requestId, ResultCode result,
                                     const std::string& detail)
{
    // Extract workerIndex from upper 8 bits of requestId.
    // 한글: requestId 상위 8비트에서 workerIndex 추출.
    const size_t workerIndex = (requestId >> 24) & 0xFFu;

    if (workerIndex >= mWorkers.size())
    {
        Logger::Error("DBServerTaskQueue::OnDBResponse: invalid workerIndex " +
                      std::to_string(workerIndex) + " from requestId " +
                      std::to_string(requestId));
        return;
    }

    WorkerData& worker = *mWorkers[workerIndex];
    {
        std::lock_guard<std::mutex> lock(worker.mutex);
        worker.responseQueue.push({requestId, result, detail});
    }
    worker.cv.notify_one();
}

// =============================================================================
// Convenience
// =============================================================================

void DBServerTaskQueue::SavePlayerProgress(
    ConnectionId sessionId,
    const std::string& jsonData,
    std::function<void(ResultCode, const std::string&)> callback)
{
    DBServerTask task;
    task.type      = DBServerTaskType::SavePlayerProgress;
    task.sessionId = sessionId;
    task.data      = jsonData;
    task.callback  = std::move(callback);
    EnqueueTask(std::move(task));
}

size_t DBServerTaskQueue::GetPendingCount() const
{
    return mPendingCount.load(std::memory_order_relaxed);
}

// =============================================================================
// WorkerThreadFunc
// =============================================================================

void DBServerTaskQueue::WorkerThreadFunc(size_t workerIndex)
{
    Logger::Info("DBServerTaskQueue worker[" + std::to_string(workerIndex) +
                 "] started");

    WorkerData& worker = *mWorkers[workerIndex];

    while (mIsRunning.load())
    {
        // Wait for any item in either queue.
        // 한글: 어느 큐에든 항목이 생길 때까지 대기.
        {
            std::unique_lock<std::mutex> lock(worker.mutex);
            worker.cv.wait(lock, [&] {
                return !worker.taskQueue.empty() ||
                       !worker.responseQueue.empty() ||
                       !mIsRunning.load();
            });
        }

        // Drain all pending responses first (prioritize responses over new tasks).
        // 한글: 응답 큐를 먼저 소진 (새 태스크보다 응답 우선).
        while (true)
        {
            DBResponseEvent resp;
            {
                std::lock_guard<std::mutex> lock(worker.mutex);
                if (worker.responseQueue.empty())
                {
                    break;
                }
                resp = std::move(worker.responseQueue.front());
                worker.responseQueue.pop();
            }
            HandleResponse(workerIndex, resp);
        }

        // Process one task from the task queue.
        // 한글: 태스크 큐에서 하나 처리.
        {
            DBServerTask task;
            bool hasTask = false;
            {
                std::lock_guard<std::mutex> lock(worker.mutex);
                if (!worker.taskQueue.empty())
                {
                    task    = std::move(worker.taskQueue.front());
                    worker.taskQueue.pop();
                    hasTask = true;
                }
            }
            if (hasTask)
            {
                mPendingCount.fetch_sub(1, std::memory_order_relaxed);
                ProcessTask(workerIndex, std::move(task));
            }
        }
    }

    Logger::Info("DBServerTaskQueue worker[" + std::to_string(workerIndex) +
                 "] stopped");
}

// =============================================================================
// ProcessTask — worker-thread only
// 한글: 워커 스레드 전용
// =============================================================================

void DBServerTaskQueue::ProcessTask(size_t workerIndex, DBServerTask task)
{
    WorkerData& worker = *mWorkers[workerIndex];

    // If session is in-flight, defer this task.
    // 한글: 세션이 in-flight 중이면 pending에 보관.
    auto it = worker.sessions.find(task.sessionId);
    if (it != worker.sessions.end() && it->second.requestId != 0)
    {
        it->second.pending.push(std::move(task));
        return;
    }

    // Check logic: internal type-based check first, then optional custom lambda.
    // 한글: 타입별 내부 검증 후 선택적 커스텀 람다 실행.
    bool checkPassed = CheckTask(task);
    if (checkPassed && task.checkFunc)
    {
        checkPassed = task.checkFunc(task);
    }

    if (!checkPassed)
    {
        auto cb = std::move(task.callback);
        // Clean up idle session entry if no pending tasks remain.
        // 한글: pending이 없으면 idle 세션 항목 정리.
        if (it != worker.sessions.end() &&
            it->second.requestId == 0 &&
            it->second.pending.empty())
        {
            worker.sessions.erase(it);
        }
        Logger::Warn("DBServerTaskQueue: CheckTask failed for session " +
                     std::to_string(task.sessionId));
        if (cb) cb(ResultCode::InvalidRequest, "Check failed");
        return;
    }

    // Generate requestId and check for wrap-around collision.
    // 한글: requestId 발급 및 랩어라운드 충돌 확인.
    const uint32_t requestId = NextRequestId(worker);

    // Ensure session state entry exists.
    // 한글: 세션 상태 항목 확보.
    SessionState& ss = worker.sessions[task.sessionId];

    if (ss.requestId != 0)
    {
        // Collision: another task for this session is already in-flight with the same requestId.
        // 한글: 동일 requestId로 이미 in-flight인 태스크 존재 — 거부.
        Logger::Error("DBServerTaskQueue: requestId collision for session " +
                      std::to_string(task.sessionId));
        auto cb = std::move(task.callback);
        if (ss.pending.empty()) worker.sessions.erase(task.sessionId);
        if (cb) cb(ResultCode::Unknown, "requestId collision");
        return;
    }

    // STORE before SEND — response may arrive before Send() returns.
    // 한글: Send 전에 반드시 저장 — Send() 반환 전에 응답이 도착할 수 있음.
    ss.requestId = requestId;
    ss.callback  = std::move(task.callback);

    // Build and send PKT_DBQueryReq.
    // 한글: PKT_DBQueryReq 패킷 조성 후 전송.
    Network::Core::PKT_DBQueryReq pkt;
    pkt.queryId    = requestId;
    pkt.taskType   = static_cast<uint8_t>(task.type);
    pkt.dataLength = static_cast<uint16_t>(
        std::min(task.data.size(), sizeof(pkt.data) - 1));
    if (pkt.dataLength > 0)
    {
        std::memcpy(pkt.data, task.data.c_str(), pkt.dataLength);
    }
    pkt.data[pkt.dataLength] = '\0';

    const bool sent = mSendFunc(&pkt, sizeof(pkt));
    if (!sent)
    {
        // Send failed — clean up session state and notify caller.
        // 한글: 전송 실패 — 세션 상태 정리 후 호출자에 알림.
        auto cb     = std::move(ss.callback);
        ss.requestId = 0;
        ss.callback  = nullptr;
        if (ss.pending.empty())
        {
            worker.sessions.erase(task.sessionId);
        }
        Logger::Error("DBServerTaskQueue: SendDBPacket failed for session " +
                      std::to_string(task.sessionId));
        if (cb) cb(ResultCode::DBServerNotConnected, "Send failed");
    }
}

// =============================================================================
// HandleResponse — worker-thread only
// 한글: 워커 스레드 전용
// =============================================================================

void DBServerTaskQueue::HandleResponse(size_t workerIndex,
                                       const DBResponseEvent& resp)
{
    WorkerData& worker = *mWorkers[workerIndex];

    // Find session by requestId — O(n) over active sessions per worker.
    // 한글: requestId로 세션 역방향 탐색 — 워커당 활성 세션 수는 소규모이므로 O(n) 허용.
    ConnectionId sessionId   = 0;
    bool         found       = false;
    for (auto& [sid, ss] : worker.sessions)
    {
        if (ss.requestId == resp.requestId)
        {
            sessionId = sid;
            found     = true;
            break;
        }
    }

    if (!found)
    {
        Logger::Warn("DBServerTaskQueue::HandleResponse: unknown requestId " +
                     std::to_string(resp.requestId));
        return;
    }

    SessionState& ss = worker.sessions[sessionId];

    // Invoke callback and clear in-flight state.
    // 한글: 콜백 호출 및 in-flight 상태 해제.
    auto cb       = std::move(ss.callback);
    ss.requestId  = 0;
    ss.callback   = nullptr;

    if (cb) cb(resp.result, resp.detail);

    // Process exactly one pending task inline (ordering guarantee).
    // 한글: pending에서 정확히 1개만 inline 처리 (순서 보장).
    if (!ss.pending.empty())
    {
        DBServerTask nextTask = std::move(ss.pending.front());
        ss.pending.pop();
        ProcessTask(workerIndex, std::move(nextTask));
    }
    else
    {
        worker.sessions.erase(sessionId);
    }
}

// =============================================================================
// ShutdownDrain — called after all worker threads are joined
// 한글: 모든 워커 스레드 join 후 호출
// =============================================================================

void DBServerTaskQueue::ShutdownDrain(size_t workerIndex)
{
    WorkerData& worker = *mWorkers[workerIndex];

    // Drain in-flight sessions and their pending queues.
    // 한글: in-flight 세션과 pending 큐 드레인.
    for (auto& [sessionId, ss] : worker.sessions)
    {
        if (ss.requestId != 0 && ss.callback)
        {
            ss.callback(ResultCode::ShuttingDown, "Server shutting down");
        }
        while (!ss.pending.empty())
        {
            auto& pendingTask = ss.pending.front();
            if (pendingTask.callback)
            {
                pendingTask.callback(ResultCode::ShuttingDown,
                                     "Server shutting down");
            }
            ss.pending.pop();
        }
    }
    worker.sessions.clear();

    // Drain unprocessed tasks from taskQueue.
    // 한글: taskQueue에 남은 처리되지 않은 태스크 드레인.
    while (!worker.taskQueue.empty())
    {
        auto& t = worker.taskQueue.front();
        if (t.callback)
        {
            t.callback(ResultCode::ShuttingDown, "Server shutting down");
        }
        worker.taskQueue.pop();
    }

    // Discard stale response events (sessions already drained above).
    // 한글: 오래된 응답 이벤트 폐기 (세션은 이미 위에서 드레인됨).
    while (!worker.responseQueue.empty())
    {
        worker.responseQueue.pop();
    }

    mPendingCount.store(0, std::memory_order_relaxed);
}

// =============================================================================
// CheckTask — type-based internal validation
// 한글: 타입별 내부 검증
// =============================================================================

bool DBServerTaskQueue::CheckTask(const DBServerTask& task)
{
    switch (task.type)
    {
    case DBServerTaskType::SavePlayerProgress:
        // Require non-empty JSON payload.
        // 한글: JSON payload 필수.
        if (task.data.empty())
        {
            Logger::Warn("DBServerTaskQueue::CheckTask: empty data for SavePlayerProgress");
            return false;
        }
        return true;

    case DBServerTaskType::LoadPlayerData:
        // Always allowed; session ID validated by routing.
        // 한글: 항상 허용; sessionId 유효성은 라우팅에서 보장.
        return true;

    default:
        Logger::Error("DBServerTaskQueue::CheckTask: unknown task type " +
                      std::to_string(static_cast<int>(task.type)));
        return false;
    }
}

// =============================================================================
// NextRequestId — worker-thread only
// 한글: 워커 스레드 전용
// =============================================================================

uint32_t DBServerTaskQueue::NextRequestId(WorkerData& worker)
{
    // Advance lower 24 bits; skip 0 so idle state (requestId==0) is unambiguous.
    // 한글: 하위 24비트 증가; 0은 idle 상태이므로 건너뜀.
    worker.seqCounter = (worker.seqCounter + 1) & 0x00FFFFFFu;
    if (worker.seqCounter == 0)
    {
        worker.seqCounter = 1;
    }
    return (static_cast<uint32_t>(worker.index) << 24) | worker.seqCounter;
}

} // namespace Network::TestServer
```

- [ ] **Step 2: 커밋**

```bash
git add Server/TestServer/src/DBServerTaskQueue.cpp
git commit -m "feat: implement DBServerTaskQueue — async task queue for TestDBServer routing"
```

---

## Chunk 3: Integration

### Task 5: DBServerPacketHandler — SetTaskQueue + HandleDBQueryResponse

**Files:**
- Modify: `Server/TestServer/include/DBServerPacketHandler.h`
- Modify: `Server/TestServer/src/DBServerPacketHandler.cpp`

- [ ] **Step 1: 헤더에 forward declaration + SetTaskQueue + 멤버 추가**

`DBServerPacketHandler.h` 상단 (기존 includes 위)에 추가:
```cpp
// Forward declaration to avoid circular includes
// 한글: 순환 include 방지를 위한 전방 선언
namespace Network::TestServer { class DBServerTaskQueue; }
```

클래스 public 섹션에 추가:
```cpp
// Inject DBServerTaskQueue for response routing
// 한글: 응답 라우팅을 위한 DBServerTaskQueue 주입
void SetTaskQueue(DBServerTaskQueue* queue) { mTaskQueue = queue; }
```

private 핸들러 목록에 추가:
```cpp
void HandleDBQueryResponse(Core::Session* session, const Core::PKT_DBQueryRes* packet);
```

private 멤버에 추가:
```cpp
DBServerTaskQueue* mTaskQueue = nullptr;
```

- [ ] **Step 2: DBServerPacketHandler.cpp — RegisterHandlers에 DBQueryRes 핸들러 등록**

`RegisterHandlers()` 끝에 추가:
```cpp
mHandlers[static_cast<uint16_t>(ServerPacketType::DBQueryRes)] =
    [this](Core::Session* session, const char* data, uint32_t size)
    {
        HandleDBQueryResponse(session,
            reinterpret_cast<const PKT_DBQueryRes*>(data));
    };
```

ProcessPacket의 requiredSize switch에 추가:
```cpp
case ServerPacketType::DBQueryRes:
    requiredSize = sizeof(PKT_DBQueryRes);
    break;
```

- [ ] **Step 3: HandleDBQueryResponse 구현 추가**

`DBServerPacketHandler.cpp` 끝 (닫는 `}` 전)에:
```cpp
void DBServerPacketHandler::HandleDBQueryResponse(
    Core::Session* session, const Core::PKT_DBQueryRes* packet)
{
    if (!packet)
    {
        Logger::Error("HandleDBQueryResponse: null packet");
        return;
    }

    if (!mTaskQueue)
    {
        Logger::Warn("HandleDBQueryResponse: no DBServerTaskQueue registered");
        return;
    }

    const ResultCode result = static_cast<ResultCode>(packet->result);
    const size_t detailLen  = std::min<size_t>(packet->detailLength,
                                                sizeof(packet->detail));
    const std::string detail(packet->detail, detailLen);

    Logger::Debug("DBQueryRes received - queryId: " +
                  std::to_string(packet->queryId) +
                  ", result: " + Network::ToString(result));

    mTaskQueue->OnDBResponse(packet->queryId, result, detail);
}
```

`DBServerPacketHandler.cpp` 상단 includes에 추가:
```cpp
#include "../include/DBServerTaskQueue.h"
#include "Interfaces/ResultCode.h"
```

- [ ] **Step 4: 커밋**

```bash
git add Server/TestServer/include/DBServerPacketHandler.h
git add Server/TestServer/src/DBServerPacketHandler.cpp
git commit -m "feat: DBServerPacketHandler — SetTaskQueue + HandleDBQueryResponse routing"
```

---

### Task 6: TestServer.h + TestServer.cpp — 연결

**Files:**
- Modify: `Server/TestServer/include/TestServer.h`
- Modify: `Server/TestServer/src/TestServer.cpp`

- [ ] **Step 1: TestServer.h — include + 멤버 추가**

`TestServer.h` includes에 추가:
```cpp
#include "DBServerTaskQueue.h"
```

TestServer 클래스 private 멤버에 추가 (`mDBTaskQueue` 근처):
```cpp
// Async task queue for routing client requests through TestDBServer
// 한글: 클라이언트 요청을 TestDBServer로 중계하는 비동기 작업 큐
std::shared_ptr<DBServerTaskQueue>          mDBServerTaskQueue;
```

- [ ] **Step 2: TestServer.cpp Initialize — DBServerTaskQueue 초기화**

`TestServer.cpp`의 `mDBTaskQueue` 초기화 블록 직후에 추가:

```cpp
// Initialize DBServerTaskQueue (routes client requests through TestDBServer)
// 한글: DBServerTaskQueue 초기화 (클라이언트 요청을 TestDBServer로 중계)
mDBServerTaskQueue = std::make_shared<DBServerTaskQueue>();
if (!mDBServerTaskQueue->Initialize(
        1,   // 1 worker is sufficient for initial deployment
        [this](const void* data, uint32_t size) -> bool {
            return SendDBPacket(data, size);
        }))
{
    Logger::Error("Failed to initialize DBServerTaskQueue");
    return false;
}
```

- [ ] **Step 3: TestServer.cpp ConnectToDBServer — SetTaskQueue 주입**

`ConnectToDBServer` 내 `mDBServerSession` 생성 직후 (line ~418):

```cpp
mDBServerSession = std::make_shared<DBServerSession>();
// Inject DBServerTaskQueue so PacketHandler can route DBQueryRes responses
// 한글: DBQueryRes 응답 라우팅을 위해 DBServerTaskQueue 주입
if (mDBServerTaskQueue)
{
    mDBServerSession->GetPacketHandler()->SetTaskQueue(mDBServerTaskQueue.get());
}
```

- [ ] **Step 4: TestServer.cpp Stop — 종료 순서 수정**

`Stop()`에서 기존 순서:
```cpp
DisconnectFromDBServer();   // joins mDBRecvThread
```

이 줄 **이후에** 추가 (DisconnectFromDBServer가 DBRecvThread를 먼저 종료):
```cpp
// Shutdown DBServerTaskQueue AFTER DBRecvThread is joined (spec §4.6)
// 한글: DBRecvThread join 후 DBServerTaskQueue 종료 (스펙 §4.6)
if (mDBServerTaskQueue && mDBServerTaskQueue->IsRunning())
{
    Logger::Info("Shutting down DBServerTaskQueue...");
    mDBServerTaskQueue->Shutdown();
}
```

- [ ] **Step 5: 커밋**

```bash
git add Server/TestServer/include/TestServer.h
git add Server/TestServer/src/TestServer.cpp
git commit -m "feat: wire DBServerTaskQueue into TestServer (init, inject, shutdown order)"
```

---

### Task 7: vcxproj + filters — 빌드 등록

**Files:**
- Modify: `Server/TestServer/TestServer.vcxproj`
- Modify: `Server/TestServer/TestServer.vcxproj.filters`

- [ ] **Step 1: TestServer.vcxproj — ClCompile + ClInclude 추가**

`<ClCompile Include="src\DBTaskQueue.cpp" />` 줄 아래에:
```xml
<ClCompile Include="src\DBServerTaskQueue.cpp" />
```

`<ClInclude Include="include\DBTaskQueue.h" />` 줄 아래에:
```xml
<ClInclude Include="include\DBServerTaskQueue.h" />
```

- [ ] **Step 2: TestServer.vcxproj.filters — Database 필터에 추가**

Database 섹션 내 기존 항목 뒤에:
```xml
<ClCompile Include="src\DBServerTaskQueue.cpp">
  <Filter>Database</Filter>
</ClCompile>
<ClInclude Include="include\DBServerTaskQueue.h">
  <Filter>Database</Filter>
</ClInclude>
```

- [ ] **Step 3: 커밋**

```bash
git add Server/TestServer/TestServer.vcxproj
git add Server/TestServer/TestServer.vcxproj.filters
git commit -m "build: add DBServerTaskQueue to TestServer vcxproj"
```

---

## Chunk 4: Build & Test

### Task 8: 빌드

- [ ] **Step 1: MSBuild 실행**

```powershell
powershell.exe -Command "& 'C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe' 'E:\\MyGitHub\\PublicStudy\\NetworkModuleTest\\NetworkModuleTest.sln' /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 10"
```

Expected: `Build succeeded.` / `0 Error(s)` / `0 Warning(s)`

오류 시: 에러 메시지를 분석하고 해당 Task로 돌아가 수정.

---

### Task 9: 기능 테스트

- [ ] **Test 1 — Check 실패 경로 (단위 검증)**

`ClientPacketHandler.cpp` 또는 테스트 코드에서:
```cpp
// data가 비어 있으면 SavePlayerProgress는 InvalidRequest 반환해야 함
DBServerTask task;
task.type      = DBServerTaskType::SavePlayerProgress;
task.sessionId = 1001;
task.data      = "";   // 빈 데이터 → CheckTask 실패
task.callback  = [](ResultCode rc, const std::string& msg) {
    Logger::Info("Check fail test: " + std::string(Network::ToString(rc)) +
                 " - " + msg);
    assert(rc == ResultCode::InvalidRequest);
};
mDBServerTaskQueue->EnqueueTask(std::move(task));
```

Expected 로그: `DBServerTaskQueue: CheckTask failed for session 1001`

- [ ] **Test 2 — 정상 경로 (서버 기동 후 검증)**

실행 순서: TestDBServer(8002) → TestServer(9000) → TestClient

TestServer 로그에서 확인:
```
DBServerTaskQueue initialized with 1 worker(s)
DBQueryRes received - queryId: ..., result: Success
```

- [ ] **Test 3 — 순서 보장 (동일 세션 연속 요청)**

동일 sessionId로 2개 태스크를 빠르게 EnqueueTask.
로그에서 두 번째 PKT_DBQueryReq 전송이 첫 번째 DBQueryRes 수신 이후임을 확인.

- [ ] **Test 4 — Shutdown (in-flight 중 종료)**

TestServer 종료 시 로그:
```
DBServerTaskQueue shutting down...
DBServerTaskQueue shutdown complete
```
in-flight callback이 `ResultCode::ShuttingDown`으로 호출됨 확인.

- [ ] **최종 커밋**

```bash
git add -A
git commit -m "feat: DBServerTaskQueue — complete implementation, build verified"
```
