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
