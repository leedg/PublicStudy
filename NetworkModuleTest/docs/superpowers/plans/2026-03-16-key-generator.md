# KeyGenerator — 충돌 없는 공용 고유 키 생성 모듈 구현 계획

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 비트 연산으로 만들던 requestId / sessionId / walSeq를 단일 공용 모듈(`KeyGenerator`)로 통일하고, 기존 32-bit 인코딩의 이론적 wrap-around 충돌을 64-bit 48-bit-seq 설계로 완전 제거.

**Architecture:** `KeyGenerator` 헤더-전용 모듈을 `ServerEngine/Utils/`에 추가. 인스턴스 모드(tag+slot 내장, per-worker 라우팅용)와 전역 모드(plain monotonic)를 제공. `DBServerTaskQueue`는 per-worker `KeyGenerator` 인스턴스를 사용하고, `OnDBResponse`에서 `GetSlot()`으로 workerIndex를 추출 — 기존 `(id >> 24) & 0xFF` 매직 시프트 대체. `PKT_DBQueryReq/Res.queryId`는 uint32_t → uint64_t로 확장.

**Tech Stack:** C++17, MSVC/VS2022, MSBuild x64 Debug, `std::atomic`, `inline static` C++17 헤더 전용 패턴

---

## 파일 구조 맵

| 파일 | 역할 | 변경 |
|---|---|---|
| `Server/ServerEngine/Utils/KeyGenerator.h` | 공용 고유키 생성 모듈 | **신규** |
| `Server/ServerEngine/ServerEngine.vcxproj` | 빌드 등록 | ClInclude 추가 |
| `Server/ServerEngine/ServerEngine.vcxproj.filters` | VS 필터 | Utils 필터 추가 |
| `Server/ServerEngine/Network/Core/ServerPacketDefine.h` | 패킷 정의 | queryId uint32_t → uint64_t |
| `Server/TestServer/include/DBServerTaskQueue.h` | 큐 헤더 | uint64_t, KeyGenerator 멤버, WorkerData 생성자 |
| `Server/TestServer/src/DBServerTaskQueue.cpp` | 큐 구현 | NextRequestId 제거, GetSlot() 사용 |
| `Server/ServerEngine/Network/Core/SessionManager.h` | 세션 관리자 헤더 | mNextSessionId 제거 |
| `Server/ServerEngine/Network/Core/SessionManager.cpp` | 세션 관리자 구현 | NextGlobalId() 사용 |
| `Server/TestServer/include/DBTaskQueue.h` | DB 작업 큐 헤더 | mWalSeq 제거 |
| `Server/TestServer/src/DBTaskQueue.cpp` | DB 작업 큐 구현 | NextGlobalId() 사용 |

---

## Chunk 1: KeyGenerator 모듈 생성

### Task 1: KeyGenerator.h 신규 생성 + vcxproj 등록

**Files:**
- Create: `Server/ServerEngine/Utils/KeyGenerator.h`
- Modify: `Server/ServerEngine/ServerEngine.vcxproj` (line ~233, ClInclude 섹션)
- Modify: `Server/ServerEngine/ServerEngine.vcxproj.filters`

- [ ] **Step 1: 파일 생성**

`E:\MyGitHub\PublicStudy\NetworkModuleTest\Server\ServerEngine\Utils\KeyGenerator.h`:

```cpp
#pragma once

// KeyGenerator — collision-free unique key generation for server components.
// 한글: 충돌 없는 고유 키 생성 공용 모듈.
//
// Key layout (uint64_t):
//   bit 63..56  tag  (8 bits) : component type  (KeyTag enum)
//   bit 55..48  slot (8 bits) : worker / instance index (0–255)
//   bit 47.. 0  seq  (48 bits): monotonic sequence (1 = first; 0 = kInvalid)
//
// Wrap-around: seq covers 2^48 = 281,474,976,710,655 values.
//   At 1,000,000 ops/s per (tag,slot): wraps after ~8,900 years.
//   0 is permanently reserved as the "invalid / unset" sentinel.
//
// Two usage patterns:
//   A) Instance mode — embeds tag + slot in the key (for routing)
//      KeyGenerator gen(KeyTag::DBQuery, workerIndex);
//      uint64_t id = gen.Next();
//      uint8_t slot = KeyGenerator::GetSlot(id);  // recover workerIndex
//
//   B) Global mode — plain monotonic unique ID (no tag/slot structure)
//      uint64_t id = KeyGenerator::NextGlobalId();

#include <atomic>
#include <cstdint>

namespace Network::Utils
{
    // =========================================================================
    // KeyTag — component type embedded in bits 63..56 of every keyed id.
    // 한글: 키 ID의 bit 63..56에 내장된 컴포넌트 타입 식별자.
    // =========================================================================

    enum class KeyTag : uint8_t
    {
        None    = 0,
        DBQuery = 1,   // DBServerTaskQueue → requestId
        Session = 2,   // SessionManager   → sessionId
        WAL     = 3,   // DBTaskQueue      → walSeq
    };

    // =========================================================================
    // KeyGenerator
    // =========================================================================

    class KeyGenerator
    {
    public:
        using KeyId = uint64_t;

        static constexpr KeyId kInvalid = 0;

        // Constructor — tag and slot are embedded in every key produced.
        // 한글: 생성자 — 모든 키에 tag와 slot이 내장됨.
        KeyGenerator(KeyTag tag, uint8_t slot) noexcept
            : mPrefix((static_cast<uint64_t>(tag)  << 56) |
                      (static_cast<uint64_t>(slot) << 48))
            , mSeq(0)
        {}

        // ── Static helpers ─────────────────────────────────────────────────

        // Extract tag (bits 63..56)
        [[nodiscard]] static KeyTag GetTag(KeyId id) noexcept
        {
            return static_cast<KeyTag>(static_cast<uint8_t>(id >> 56));
        }

        // Extract slot (bits 55..48)
        [[nodiscard]] static uint8_t GetSlot(KeyId id) noexcept
        {
            return static_cast<uint8_t>((id >> 48) & 0xFFu);
        }

        // Extract sequence (bits 47..0)
        [[nodiscard]] static uint64_t GetSeq(KeyId id) noexcept
        {
            return id & kSeqMask;
        }

        [[nodiscard]] static bool IsValid(KeyId id) noexcept
        {
            return (id & kSeqMask) != 0;
        }

        // ── Pattern B: global unique ID ────────────────────────────────────
        // Plain monotonic uint64_t. Thread-safe, lock-free. Starts at 1.
        // 한글: 단순 단조 증가 uint64_t. 락-프리, 1부터 시작.
        [[nodiscard]] static KeyId NextGlobalId() noexcept
        {
            return sGlobalSeq.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        // ── Pattern A: instance mode (tag + slot embedded) ─────────────────
        // Returns next id with tag+slot embedded. Thread-safe, lock-free.
        // seq wraps 0→1 (skips 0). Practical wrap: ~8,900 years at 1M/s.
        // 한글: tag+slot 내장 ID 반환. seq가 0으로 wrap되면 1로 재시작.
        [[nodiscard]] KeyId Next() noexcept
        {
            uint64_t seq = mSeq.fetch_add(1, std::memory_order_relaxed) + 1;
            seq &= kSeqMask;
            if (seq == 0) { seq = 1; }   // skip 0 (kInvalid sentinel)
            return mPrefix | seq;
        }

    private:
        static constexpr uint64_t kSeqMask = (1ULL << 48) - 1;

        uint64_t              mPrefix;  // pre-computed tag+slot bits (immutable)
        std::atomic<uint64_t> mSeq;     // per-instance sequence counter

        // C++17 inline static — ODR-safe, no separate .cpp definition needed.
        // 한글: C++17 inline static — 헤더 정의로 ODR 안전.
        inline static std::atomic<KeyId> sGlobalSeq{0};
    };

} // namespace Network::Utils
```

- [ ] **Step 2: ServerEngine.vcxproj에 ClInclude 추가**

`Server/ServerEngine/ServerEngine.vcxproj` 에서 다음 줄:
```xml
    <ClInclude Include="Utils\NetworkTypes.h" />
```
바로 위에 추가:
```xml
    <ClInclude Include="Utils\KeyGenerator.h" />
```

- [ ] **Step 3: ServerEngine.vcxproj.filters에 추가**

`Server/ServerEngine/ServerEngine.vcxproj.filters` 에서 다음 줄:
```xml
    <ClInclude Include="Utils\NetworkTypes.h">
```
바로 위에 추가:
```xml
    <ClInclude Include="Utils\KeyGenerator.h">
      <Filter>Utils</Filter>
    </ClInclude>
```

- [ ] **Step 4: 커밋**

```bash
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" add Server/ServerEngine/Utils/KeyGenerator.h Server/ServerEngine/ServerEngine.vcxproj Server/ServerEngine/ServerEngine.vcxproj.filters
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" commit -m "feat: add KeyGenerator — collision-free 64-bit key module (tag|slot|seq48)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Chunk 2: Packet + DBServerTaskQueue 업데이트

### Task 2: ServerPacketDefine.h — queryId uint32_t → uint64_t

**Files:**
- Modify: `Server/ServerEngine/Network/Core/ServerPacketDefine.h`

현재 `PKT_DBQueryReq`:
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

현재 `PKT_DBQueryRes`:
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

- [ ] **Step 1: PKT_DBQueryReq::queryId uint32_t → uint64_t**

`PKT_DBQueryReq` 구조체 전체를 다음으로 교체:

```cpp
    struct PKT_DBQueryReq
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBQueryReq;

        ServerPacketHeader header;
        uint64_t  queryId;       // requestId (KeyGenerator::KeyId: tag|slot|seq48)
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

- [ ] **Step 2: PKT_DBQueryRes::queryId uint32_t → uint64_t**

`PKT_DBQueryRes` 구조체 전체를 다음으로 교체:

```cpp
    struct PKT_DBQueryRes
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBQueryRes;

        ServerPacketHeader header;
        uint64_t  queryId;       // requestId echo (KeyGenerator::KeyId)
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
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" add Server/ServerEngine/Network/Core/ServerPacketDefine.h
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" commit -m "feat: extend PKT_DBQueryReq/Res.queryId to uint64_t (KeyGenerator::KeyId)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 3: DBServerTaskQueue.h — KeyGenerator 적용

**Files:**
- Modify: `Server/TestServer/include/DBServerTaskQueue.h`

- [ ] **Step 1: KeyGenerator.h include 추가**

기존 include 블록에 추가 (ResultCode.h 위에):
```cpp
#include "Utils/KeyGenerator.h"
```

- [ ] **Step 2: DBResponseEvent::requestId uint32_t → uint64_t**

```cpp
        struct DBResponseEvent
        {
            uint64_t    requestId = 0;      // KeyGenerator::KeyId (was uint32_t)
            ResultCode  result    = ResultCode::Unknown;
            std::string detail;
        };
```

- [ ] **Step 3: SessionState::requestId uint32_t → uint64_t**

```cpp
        struct SessionState
        {
            uint64_t  requestId = 0;   // 0 = idle (KeyGenerator::kInvalid); was uint32_t
            std::function<void(ResultCode, const std::string&)> callback;
            std::queue<DBServerTask> pending;
        };
```

- [ ] **Step 4: WorkerData 수정 — 생성자 추가, seqCounter 제거, keyGen 추가**

기존 WorkerData:
```cpp
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
```

신규 WorkerData:
```cpp
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
```

- [ ] **Step 5: OnDBResponse 시그니처 변경 + NextRequestId 선언 제거**

기존:
```cpp
        void OnDBResponse(uint32_t requestId, ResultCode result,
                          const std::string& detail);
        ...
        uint32_t NextRequestId(WorkerData& worker);
```

신규:
```cpp
        void OnDBResponse(uint64_t requestId, ResultCode result,
                          const std::string& detail);
```
(`NextRequestId` 선언 삭제 — KeyGenerator가 대체)

- [ ] **Step 6: 커밋**

```bash
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" add Server/TestServer/include/DBServerTaskQueue.h
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" commit -m "refactor: DBServerTaskQueue — KeyGenerator replaces bit-encoded seqCounter

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 4: DBServerTaskQueue.cpp — KeyGenerator 적용

**Files:**
- Modify: `Server/TestServer/src/DBServerTaskQueue.cpp`

- [ ] **Step 1: Initialize() — WorkerData 생성자 호출로 변경**

기존:
```cpp
    for (size_t i = 0; i < workerCount; ++i)
    {
        auto wd    = std::make_unique<WorkerData>();
        wd->index  = i;
        mWorkers.push_back(std::move(wd));
    }
```

신규:
```cpp
    for (size_t i = 0; i < workerCount; ++i)
    {
        mWorkers.push_back(std::make_unique<WorkerData>(i));
    }
```

- [ ] **Step 2: OnDBResponse() 시그니처 + GetSlot() 사용**

기존:
```cpp
void DBServerTaskQueue::OnDBResponse(uint32_t requestId, ResultCode result,
                                     const std::string& detail)
{
    // Extract workerIndex from upper 8 bits of requestId.
    // 한글: requestId 상위 8비트에서 workerIndex 추출.
    const size_t workerIndex = (requestId >> 24) & 0xFFu;
```

신규:
```cpp
void DBServerTaskQueue::OnDBResponse(uint64_t requestId, ResultCode result,
                                     const std::string& detail)
{
    // Extract workerIndex from slot field via KeyGenerator helper.
    // 한글: KeyGenerator 헬퍼로 slot 필드에서 workerIndex 추출 (매직 시프트 제거).
    const size_t workerIndex = static_cast<size_t>(
        Utils::KeyGenerator::GetSlot(requestId));
```

- [ ] **Step 3: ProcessTask() — NextRequestId 제거, keyGen.Next() 사용**

기존:
```cpp
    // Generate requestId and check for wrap-around collision.
    // 한글: requestId 발급 및 랩어라운드 충돌 확인.
    const uint32_t requestId = NextRequestId(worker);
```

신규:
```cpp
    // Generate collision-free requestId via KeyGenerator (tag=DBQuery, slot=workerIndex).
    // 한글: KeyGenerator로 충돌 없는 requestId 발급 (48-bit seq, wrap ~8,900년 @ 1M/s).
    const uint64_t requestId = worker.keyGen.Next();
```

- [ ] **Step 4: ProcessTask() — ss.requestId 타입 자동 반영 확인**

아래 두 줄은 코드 변경 없이 uint64_t로 자동 적용됨 (SessionState 변경 반영):
```cpp
    ss.requestId = requestId;   // uint64_t = uint64_t
    ss.callback  = std::move(task.callback);
```

그리고:
```cpp
    pkt.queryId = requestId;    // PKT_DBQueryReq::queryId = uint64_t
```

- [ ] **Step 5: NextRequestId() 함수 전체 삭제**

파일 하단의 `NextRequestId` 구현 전체(약 12줄)를 삭제:
```cpp
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
```

- [ ] **Step 6: assert 코멘트 업데이트**

기존 assert 주석의 "requestId wrap-around collision" 언급을 KeyGenerator 참조로 갱신:
```cpp
    // Invariant: session must be idle here — in-flight case was already handled above.
    // 한글: 여기서 세션은 반드시 idle — in-flight 케이스는 위에서 이미 처리됨.
    // (KeyGenerator::Next() guarantees no seq reuse within the lifetime of any
    //  realistic session — 48-bit seq wraps after ~8,900 years at 1M ops/s.)
    assert(ss.requestId == 0);
```

- [ ] **Step 7: 커밋**

```bash
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" add Server/TestServer/src/DBServerTaskQueue.cpp
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" commit -m "refactor: DBServerTaskQueue.cpp — use KeyGenerator.Next() + GetSlot(), remove NextRequestId

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Chunk 3: SessionManager + DBTaskQueue 업데이트

### Task 5: SessionManager — NextGlobalId() 적용

**Files:**
- Modify: `Server/ServerEngine/Network/Core/SessionManager.h`
- Modify: `Server/ServerEngine/Network/Core/SessionManager.cpp`

- [ ] **Step 1: SessionManager.h — mNextSessionId 제거**

기존 (SessionManager.h private 섹션):
```cpp
    std::atomic<Utils::ConnectionId> mNextSessionId{1};
```

삭제 (KeyGenerator::NextGlobalId()가 대체).

- [ ] **Step 2: SessionManager.cpp — include 추가 + GenerateSessionId 구현 교체**

파일 상단 include 블록에 추가:
```cpp
#include "Utils/KeyGenerator.h"
```

기존 `GenerateSessionId()` 구현:
```cpp
Utils::ConnectionId SessionManager::GenerateSessionId()
{
	return mNextSessionId.fetch_add(1);
}
```

신규:
```cpp
Utils::ConnectionId SessionManager::GenerateSessionId()
{
    // KeyGenerator::NextGlobalId() — monotonic uint64_t, lock-free, starts at 1.
    // 한글: KeyGenerator 전역 ID — 단조 증가 uint64_t, 락-프리, 1부터 시작.
    return Utils::KeyGenerator::NextGlobalId();
}
```

- [ ] **Step 3: 커밋**

```bash
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" add Server/ServerEngine/Network/Core/SessionManager.h Server/ServerEngine/Network/Core/SessionManager.cpp
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" commit -m "refactor: SessionManager — use KeyGenerator::NextGlobalId() for session IDs

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 6: DBTaskQueue — NextGlobalId() for WAL seq

**Files:**
- Modify: `Server/TestServer/include/DBTaskQueue.h`
- Modify: `Server/TestServer/src/DBTaskQueue.cpp`

- [ ] **Step 1: DBTaskQueue.h — mWalSeq 제거**

기존 (DBTaskQueue.h private 멤버):
```cpp
        std::atomic<uint64_t>           mWalSeq{0};     // 단조 증가 시퀀스 번호
```

삭제 (KeyGenerator::NextGlobalId()가 대체).

- [ ] **Step 2: DBTaskQueue.cpp — include 추가 + WalNextSeq 구현 교체**

파일 상단 include 블록에 추가:
```cpp
#include "Utils/KeyGenerator.h"
```

기존 `WalNextSeq()` 구현:
```cpp
uint64_t DBTaskQueue::WalNextSeq()
{
    // Monotonically increasing, unique sequence number
    // 한글: 단조 증가 고유 시퀀스 번호
    return mWalSeq.fetch_add(1, std::memory_order_relaxed) + 1;
}
```

신규:
```cpp
uint64_t DBTaskQueue::WalNextSeq()
{
    // KeyGenerator::NextGlobalId() — shared monotonic counter, lock-free.
    // 한글: KeyGenerator 전역 단조 증가 — WAL 시퀀스 번호 발급.
    return Utils::KeyGenerator::NextGlobalId();
}
```

- [ ] **Step 3: 커밋**

```bash
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" add Server/TestServer/include/DBTaskQueue.h Server/TestServer/src/DBTaskQueue.cpp
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" commit -m "refactor: DBTaskQueue — use KeyGenerator::NextGlobalId() for WAL sequence

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Chunk 4: 빌드 + 검증

### Task 7: MSBuild + 자가 테스트

- [ ] **Step 1: MSBuild 실행**

```powershell
powershell.exe -Command "& 'C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe' 'E:\\MyGitHub\\PublicStudy\\NetworkModuleTest\\NetworkModuleTest.sln' /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 10"
```

Expected: `빌드했습니다.` / `0 오류` / `0 경고`

- [ ] **Step 2: --self-test 실행**

```bash
"E:\MyGitHub\PublicStudy\NetworkModuleTest\x64\Debug\TestServer.exe" --self-test -l DEBUG
```

Expected 로그:
```
[INFO] DBServerTaskQueue initialized with 1 worker(s)
[INFO] DBServerTaskQueue worker[0] started
[WARN] DBServerTaskQueue::CheckTask: empty data for SavePlayerProgress
[WARN] DBServerTaskQueue: CheckTask failed for session 9999
[INFO] [SelfTest] Check-fail callback: rc=InvalidRequest msg=Check failed
[INFO] [SelfTest] PASS: empty-data check correctly returns InvalidRequest
[INFO] === DBServerTaskQueue SelfTest complete ===
[INFO] DBServerTaskQueue shutting down...
[INFO] DBServerTaskQueue worker[0] stopped
[INFO] DBServerTaskQueue shutdown complete
```

- [ ] **Step 3: 최종 커밋 (빌드 아티팩트 제외)**

```bash
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" status
git -C "E:\MyGitHub\PublicStudy\NetworkModuleTest" commit -m "feat: KeyGenerator migration complete — build verified, self-test PASS

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

(새로 수정된 파일이 있는 경우에만 커밋)

---

## 변경 요약

| 개선 항목 | 변경 전 | 변경 후 |
|---|---|---|
| requestId 비트폭 | 32-bit (workerIndex:8 \| seq:24) | 64-bit (tag:8 \| slot:8 \| seq:48) |
| wrap-around 주기 | ~4.7시간 @ 1k req/s | ~8,900년 @ 1M req/s |
| workerIndex 추출 | `(id >> 24) & 0xFF` 매직 시프트 | `KeyGenerator::GetSlot(id)` |
| 충돌 감지 | Debug-only assert | 실질적 불가 (48-bit seq) |
| 세션 ID 생성 | `mNextSessionId.fetch_add(1)` | `KeyGenerator::NextGlobalId()` |
| WAL seq 생성 | `mWalSeq.fetch_add(1) + 1` | `KeyGenerator::NextGlobalId()` |
| 공용 모듈 | 없음 | `ServerEngine/Utils/KeyGenerator.h` |
