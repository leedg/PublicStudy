# DBServerTaskQueue 설계 스펙

**Date:** 2026-03-16
**Status:** Approved (v2 — post spec-review)

---

## 1. 목적

TestServer에서 클라이언트 요청을 TestDBServer로 중계하는 비동기 작업 큐.
기존 `DBTaskQueue`(로컬 DB 직접 호출)와 별개로, TestDBServer와의 서버간 통신을 담당한다.

---

## 2. 처리 경로 정의

```
[경로 A] 클라이언트 패킷 → Logic Worker → 클라이언트 직접 응답
[경로 B] 클라이언트 연결/해제 → Logic Worker → DBTaskQueue Worker → 로컬 DB 직접 호출
[경로 C] 클라이언트 요청 → DBServerTaskQueue Worker → TestDBServer → 클라이언트 응답  ← 신규
[경로 D] TestServer ↔ TestDBServer 서버간 Ping (기존 유지, 변경 없음)
```

---

## 3. DBServerTaskQueue 동작 흐름

```
[클라이언트 요청]
      │  EnqueueTask(DBServerTask)
      ▼
DBServerTaskQueue::WorkerThread  (sessionId % workerCount 라우팅)
  ├─ sessions[sessionId].requestId != 0  →  pending.push(task)  [대기]
  └─ idle (requestId == 0)
        ├─ CheckLogic 실패  →  callback(ResultCode::InvalidRequest, reason)  [즉시]
        └─ CheckLogic 통과
              │  requestId = (workerIndex << 24) | seq++
              │  sessions[sessionId] = { requestId, callback }   ← store FIRST
              │  Send PKT_DBQueryReq { requestId(=queryId), taskType, data }
              ▼
        [TestDBServer 처리 중...]
              │
              ▼  PKT_DBQueryRes { queryId=requestId, result, detail }
DBServerPacketHandler::HandleDBQueryResponse()
      │  workerIndex = (queryId >> 24) & 0xFF       ← requestId 인코딩으로 추출
      │  worker[N].responseQueue.push(DBResponseEvent{ requestId, result, detail })
      │  worker[N].cv.notify_one()
      ▼
WorkerThread[N]:
  responseQueue 우선 소진 (mutex 없이 worker.sessions 접근)
  → sessions에서 requestId로 sessionId 역방향 조회 (per-worker O(n), 실용 범위)
  → callback(result, detail)
  → sessions[sessionId].pending 비어있으면 erase
  → pending 있으면 다음 1개만 inline 처리 (순서 보장, 재귀 방지)
```

> **주의:** `sessions` 역방향 조회(requestId→sessionId)는 per-worker 기준이며,
> 동일 워커에 배정된 활성 세션 수는 통상 수십 개 이하이므로 O(n) 순회 허용.
> 성능이 문제될 경우 `WorkerData`에 `requestId→sessionId` 역방향 map 추가.

---

## 4. 핵심 설계 결정

### 4.1 per-session 순서 보장

- key-affinity 라우팅: `sessionId % workerCount` → 동일 세션은 항상 동일 워커
- 세션 X가 in-flight 중이면 후속 task는 `sessions[X].pending`에 보관
- 응답 수신 후 pending task를 **1개만** inline 처리 (queue 재주입 없음 → 순서 역전 방지)
- **callback 내부에서 동일 세션으로 `EnqueueTask` 재호출 금지** (pending 중간 삽입 방지)

### 4.2 뮤텍스 범위 최소화

| 상태 | 접근 | 뮤텍스 |
|---|---|---|
| `WorkerData::taskQueue` | EnqueueTask(외부) + worker | worker.mutex |
| `WorkerData::responseQueue` | DBRecvThread + worker | worker.mutex |
| `WorkerData::sessions` | worker 단독 | 불필요 |

글로벌 공유 상태 없음.

### 4.3 requestId 인코딩

```
requestId (uint32_t) = (workerIndex << 24) | sequentialId
```
- workerIndex: 상위 8비트 (workerCount ≤ 255, 초기화 시 static_assert)
- sequentialId: 하위 24비트, per-worker atomic 카운터
- **DBRecvThread는 `(queryId >> 24) & 0xFF`로 workerIndex 추출** → 별도 lookup 없음
- sequentialId 랩어라운드 시 동일 requestId가 sessions에 이미 존재하면 오류 로그 후 거부

### 4.4 Store-before-Send 순서 (필수)

```cpp
sessions[sessionId] = { requestId, callback };  // 반드시 먼저
SendToDBServer(packet);                          // 이후 전송
```
응답이 극히 빠르게 도착해도 sessions 조회 실패 없음.

### 4.5 deferred task inline 처리 (1개 제한)

ResponseEvent 처리 시 pending에서 **1개만** 꺼내 inline 처리.
처리된 task가 DBServer에 전송되면 다시 in-flight 상태가 되며, 다음 pending은 그 다음 응답 때 처리된다.
이로써 worker thread가 동일 세션의 pending 체인을 전부 소진하는 blocking loop가 발생하지 않음.

### 4.6 종료(Shutdown) 순서

```
1. TestServer::Stop()
2. DisconnectFromDBServer()  →  mDBRecvThread.join()  ← DBRecvThread 먼저 종료
3. mDBServerTaskQueue->Shutdown()                      ← 그 다음 큐 종료
```
DBRecvThread가 완전히 종료된 이후에 큐를 내리므로,
종료 후 responseQueue에 새 항목이 주입되지 않음.

Shutdown 시 각 워커는:
- 남은 taskQueue/responseQueue 드레인
- `sessions`에 in-flight 중인 항목(`requestId != 0`)의 callback을 `ResultCode::ShuttingDown`으로 호출
- `sessions`의 pending task들도 순서대로 `ResultCode::ShuttingDown` callback 호출

### 4.7 패킷 설계 원칙

`PKT_DBQueryReq`/`PKT_DBQueryRes`는 **기존 구조체를 재설계** (breaking change).
`PKT_DBSavePingTimeReq`/`PKT_DBSavePingTimeRes` 경로(경로 D)는 변경 없음.

고정 크기 구조체 오버헤드:
- `PKT_DBQueryReq`: ~531 bytes (항상 전송)
- `PKT_DBQueryRes`: ~278 bytes
- 4096 byte 상한 내, 현재 규모에서 허용. 가변 길이 프레이밍은 향후 개선 항목.

### 4.8 DBServerTaskQueue 주입 경로

```
TestServer::Initialize()
  → mDBServerTaskQueue 생성
  → mDBServerSession->GetPacketHandler()->SetTaskQueue(mDBServerTaskQueue.get())
```
`DBServerPacketHandler`가 `DBServerTaskQueue*`를 멤버로 보유.
`DBServerSession.cpp`는 변경 불필요 (OnRecv → PacketHandler 체인 변경 없음).

---

## 5. 데이터 구조

### ResultCode (공용 enum)

```cpp
// Server/ServerEngine/Interfaces/ResultCode.h
enum class ResultCode : int32_t
{
    Success               = 0,
    // 공통 오류 (1~999)
    Unknown               = 1,
    InvalidRequest        = 2,
    Timeout               = 3,
    NotInitialized        = 4,
    ShuttingDown          = 5,
    // 세션/인증 (1000~1999)
    SessionNotFound       = 1000,
    SessionExpired        = 1001,
    NotAuthenticated      = 1002,
    PermissionDenied      = 1003,
    // DB 오류 (2000~2999)
    DBConnectionFailed    = 2000,
    DBQueryFailed         = 2001,
    DBRecordNotFound      = 2002,
    DBDuplicateKey        = 2003,
    DBTransactionFailed   = 2004,
    // 서버간 통신 오류 (3000~3999)
    DBServerNotConnected  = 3000,
    DBServerTimeout       = 3001,
    DBServerRejected      = 3002,
    // 게임 로직 오류 (4000~4999)
    InsufficientResources = 4000,
    InvalidGameState      = 4001,
    ItemNotFound          = 4002,
    LevelRequirementNotMet = 4003,
};

inline bool IsSuccess(ResultCode c) { return c == ResultCode::Success; }
const char* ToString(ResultCode c);
```

### DBServerTask

```cpp
enum class DBServerTaskType : uint8_t {
    SavePlayerProgress = 0,
    LoadPlayerData     = 1,
};

struct DBServerTask {
    DBServerTaskType  type;
    ConnectionId      sessionId;
    std::string       data;       // JSON payload
    // 선택: 커스텀 검증 람다 (없으면 타입별 내부 핸들러)
    std::function<bool(const DBServerTask&)>            checkFunc;
    std::function<void(ResultCode, const std::string&)> callback;
    // requestId는 워커가 발급 — 호출자가 설정하지 않음
};
```

### WorkerData

```cpp
struct DBResponseEvent {
    uint32_t      requestId;
    ResultCode    result;
    std::string   detail;
};

struct SessionState {
    uint32_t  requestId = 0;    // 0 = idle
    std::function<void(ResultCode, const std::string&)> callback;
    std::queue<DBServerTask> pending;
};

struct WorkerData {
    // 공유 (mutex 보호)
    std::queue<DBServerTask>    taskQueue;
    std::queue<DBResponseEvent> responseQueue;
    mutable std::mutex          mutex;
    std::condition_variable     cv;
    std::thread                 thread;

    // 워커 전용 (mutex 불필요)
    std::unordered_map<ConnectionId, SessionState> sessions;
};
```

---

## 6. 패킷 변경

```cpp
// PKT_DBQueryReq — 재설계 (breaking change)
struct PKT_DBQueryReq {
    ServerPacketHeader header;
    uint32_t  queryId;       // requestId ((workerIndex<<24)|seq)
    uint8_t   taskType;      // DBServerTaskType
    uint16_t  dataLength;
    char      data[512];
};

// PKT_DBQueryRes — 재설계 (breaking change)
struct PKT_DBQueryRes {
    ServerPacketHeader header;
    uint32_t   queryId;      // requestId echo
    int32_t    result;       // ResultCode
    uint16_t   detailLength;
    char       detail[256];
};
```

`sessionId`는 패킷에 포함하지 않음 — `requestId`의 상위 8비트로 workerIndex를 추출하고,
sessionId 역방향 조회는 WorkerData::sessions에서 수행.

---

## 7. 변경 파일 목록

| 파일 | 변경 |
|---|---|
| `ServerEngine/Interfaces/ResultCode.h` | 신규 — 공용 ResultCode enum |
| `ServerEngine/Network/Core/ServerPacketDefine.h` | PKT_DBQueryReq/Res 재설계 (sessionId 제거, taskType/ResultCode 추가) |
| `TestServer/include/DBServerTaskQueue.h` | 신규 |
| `TestServer/src/DBServerTaskQueue.cpp` | 신규 |
| `TestServer/include/DBServerPacketHandler.h` | SetTaskQueue() 추가, DBServerTaskQueue* 멤버 |
| `TestServer/src/DBServerPacketHandler.cpp` | HandleDBQueryResponse → DBServerTaskQueue 라우팅 |
| `TestServer/include/TestServer.h` | mDBServerTaskQueue 멤버 추가 |
| `TestServer/src/TestServer.cpp` | 초기화/주입/종료 순서 (DBRecvThread.join → Shutdown 순) |
| `TestServer/TestServer.vcxproj` | DBServerTaskQueue.cpp 빌드 추가 |
| `TestServer/TestServer.vcxproj.filters` | 필터 추가 |

---

## 8. 테스트 계획

1. **빌드**: 0 errors, 0 warnings
2. **Check 실패 경로**: 검증 실패 시 `callback(ResultCode::InvalidRequest, ...)` 즉시 호출 확인
3. **정상 경로**: DBServer 연결 → SavePlayerProgress 요청 → DBServer 처리 → 응답 수신 → `callback(ResultCode::Success, ...)` 호출
4. **순서 보장**: 동일 세션 연속 2개 task → Task A 응답 전 Task B 전송 안 됨 확인 (log 검증)
5. **pending 처리**: Task A 응답 후 Task B 자동 처리 및 callback 호출 확인
6. **Shutdown**: in-flight 중 Shutdown 시 `callback(ResultCode::ShuttingDown, ...)` 호출 확인
