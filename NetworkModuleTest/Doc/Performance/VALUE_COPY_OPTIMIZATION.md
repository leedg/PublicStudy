# 값 복사 최적화 (Value Copy Optimization)

## 작성일: 2026-02-06

## 개요
네트워크 엔진 전반에서 불필요한 값 복사를 최소화하여 성능을 향상시킨 최적화 작업 내용입니다.

---

## 적용된 최적화

### 1. SafeQueue - Move Semantics 추가 ✅

**파일**: `Server/ServerEngine/Utils/SafeQueue.h`

**변경 내용**:
```cpp
// 기존: 복사만 지원
void Push(const T &item);

// 추가: 이동 지원
void Push(T &&item);

// 추가: 직접 생성 지원
template<typename... Args>
void Emplace(Args&&... args);
```

**효과**:
- 임시 객체 전달 시 복사 대신 이동 사용
- `Emplace`로 큐 내부에서 객체 직접 생성
- 큰 객체(std::string, std::vector 등) 성능 향상

---

### 2. DBTaskQueue::EnqueueTask - rvalue reference ✅

**파일**:
- `Server/TestServer/include/DBTaskQueue.h`
- `Server/TestServer/src/DBTaskQueue.cpp`

**변경 내용**:
```cpp
// 변경 전: 값으로 전달 (복사 발생)
void EnqueueTask(DBTask task);

// 변경 후: rvalue reference (이동)
void EnqueueTask(DBTask&& task);
```

**호출 코드**:
```cpp
// 임시 객체가 자동으로 이동됨 (복사 없음)
EnqueueTask(DBTask(DBTaskType::RecordConnectTime, sessionId, timestamp));
```

**효과**:
- DBTask 객체 복사 완전히 제거
- std::string data, std::function callback 복사 방지
- 고빈도 호출 함수이므로 누적 성능 향상 큼

---

### 3. SessionManager::ForEachSession - const reference ✅

**파일**:
- `Server/ServerEngine/Network/Core/SessionManager.h`
- `Server/ServerEngine/Network/Core/SessionManager.cpp`

**변경 내용**:
```cpp
// 변경 전: std::function 복사
void ForEachSession(std::function<void(SessionRef)> func);

// 변경 후: const reference
void ForEachSession(const std::function<void(SessionRef)>& func);
```

**효과**:
- std::function 객체 복사 방지
- 람다 캡처가 큰 경우 성능 향상
- 함수 호출 오버헤드 감소

---

## 이미 최적화된 코드

### Session::Send() - std::move 사용 ✅

**코드**:
```cpp
void Session::Send(const void *data, uint32_t size)
{
    std::vector<char> buffer(size);
    std::memcpy(buffer.data(), data, size);

    {
        std::lock_guard<std::mutex> lock(mSendMutex);
        mSendQueue.push(std::move(buffer));  // 이미 move 사용 중 ✅
        mSendQueueSize.fetch_add(1, std::memory_order_relaxed);
    }

    FlushSendQueue();
}
```

**상태**: 이미 최적화됨

---

### SessionRef (shared_ptr) - 복사 비용 낮음 ✅

**타입**:
```cpp
using SessionRef = std::shared_ptr<Session>;
```

**특징**:
- shared_ptr 복사는 참조 카운터 증가만 수행
- atomic 연산이지만 비용이 낮음
- 복사 필요성이 있는 경우 (여러 곳에서 참조)
- 추가 최적화 불필요

---

## 성능 영향 분석

### 고빈도 호출 함수 (High Impact)

1. **DBTaskQueue::EnqueueTask**
   - 모든 DB 작업마다 호출
   - DBTask 복사 → 이동으로 변경
   - **예상 개선**: 30-50% (DBTask 크기에 따라)

2. **Session::Send**
   - 모든 패킷 전송마다 호출
   - **이미 최적화됨** (std::move 사용 중)

### 중빈도 호출 함수 (Medium Impact)

3. **SessionManager::ForEachSession**
   - 브로드캐스트, 정리 작업 시 호출
   - std::function 복사 → const reference
   - **예상 개선**: 10-20%

4. **SafeQueue::Push/Emplace**
   - 범용 큐 작업
   - Move semantics 지원 추가
   - **예상 개선**: 20-40% (T의 크기에 따라)

---

## 추가 검토 대상

### 검토 완료
- ✅ Session::Send() - 이미 최적화됨
- ✅ SessionManager - ForEachSession 최적화 완료
- ✅ DBTaskQueue - EnqueueTask 최적화 완료
- ✅ SafeQueue - Move semantics 추가 완료

### 향후 검토 필요 (낮은 우선순위)
- Logger 클래스의 메시지 전달 방식
- Timer 콜백 함수 전달 방식

---

## 요약

### 적용된 최적화 (2026-02-06)
1. SafeQueue: Push(T&&), Emplace(Args&&...) 추가
2. DBTaskQueue::EnqueueTask: 값 전달 → rvalue reference
3. SessionManager::ForEachSession: 값 전달 → const reference

### 성능 향상 예상
- DBTaskQueue: 30-50% (고빈도 호출)
- SafeQueue: 20-40% (객체 크기에 따라)
- ForEachSession: 10-20% (std::function 복사 제거)

### 코드 품질
- Move semantics를 활용한 Zero-copy 패턴
- 현대적인 C++ 스타일 (C++11/14/17)
- 한글/영어 주석 유지

---

---

# 메모리 풀 고도화 최적화 (2026-03-01)

## 배경

WSA 10055 수정(RIO slab pool 도입, 2026-02-28)으로 1000/1000 연결 PASS 달성 후,
서버 메모리 풀 구조의 남은 비효율 3곳을 추가 개선하였다.

---

## Step 1 — AsyncBufferPool O(1) 프리리스트 교체 ✅

**파일**: `Server/ServerEngine/Platforms/AsyncBufferPool.h/.cpp`

### 변경 전 (O(n) 선형 탐색)

```cpp
// Acquire: O(n) 선형 스캔
for (auto& slot : mSlots) {
    if (!slot.inUse) { slot.inUse = true; return &slot; }
}

// Release: O(n) bufferId로 탐색
for (auto& slot : mSlots) {
    if (slot.bufferId == id) { slot.inUse = false; return; }
}
```

### 변경 후 (O(1) 프리리스트)

추가 멤버:
```cpp
std::vector<size_t>                 mFreeIndices;      // O(1) pop/push 스택
std::unordered_map<int64_t, size_t> mBufferIdToIndex;  // O(1) bufferId → 슬롯 인덱스
```

`Initialize()` 확장:
```cpp
mFreeIndices.resize(poolSize);
std::iota(mFreeIndices.begin(), mFreeIndices.end(), size_t(0));
mBufferIdToIndex.reserve(poolSize);
for (size_t i = 0; i < mSlots.size(); ++i)
    mBufferIdToIndex[mSlots[i].bufferId] = i;
```

`Acquire()` — O(1):
```cpp
const size_t idx = mFreeIndices.back();
mFreeIndices.pop_back();
mSlots[idx].inUse = true;
```

`Release()` — O(1):
```cpp
const auto it = mBufferIdToIndex.find(bufferId);
mSlots[it->second].inUse = false;
mFreeIndices.push_back(it->second);
```

**효과**: 슬롯 수(N) 증가에 무관하게 Acquire/Release 상수 시간 보장

---

## Step 2 — ProcessRawRecv 배치 평탄 버퍼 + Zero-Alloc 패스트패스 ✅

**파일**: `Server/ServerEngine/Network/Core/Session.cpp` — `ProcessRawRecv()`

### 변경 전 (N 패킷 = N 힙 할당)

```cpp
std::vector<std::vector<char>> completePackets;
// 루프 내: completePackets.push_back(std::vector<char>(data, data+size))
// → N 패킷 = N 번의 vector 생성/해제
```

### 변경 후

**패스트패스** (축적 데이터 없고 수신 데이터가 정확히 1패킷인 경우):
```cpp
// 힙 alloc 전혀 없이 직접 OnRecv 호출
bool fastPath = false;
{ std::lock_guard<std::mutex> lk(mRecvMutex);
  if (mRecvAccumBuffer.empty() && size >= sizeof(PacketHeader)) {
      const auto* hdr = reinterpret_cast<const PacketHeader*>(data);
      if (hdr->size == size) fastPath = true;
  }
}
if (fastPath) { OnRecv(data, size); return; }
```

**일반 경로** — 배치 평탄 버퍼 (1 alloc for N packets):
```cpp
struct PacketSpan { uint32_t offset; uint32_t size; };
std::vector<char>       batchBuf;  // 1회 reserve
std::vector<PacketSpan> spans;

// 파싱 루프: batchBuf에 연속 복사, spans에 offset+size 기록
for (const auto& sp : spans)
    OnRecv(batchBuf.data() + sp.offset, sp.size);
```

**효과**: N 패킷 처리 시 힙 할당 횟수 N → 1 (최빈 단일 패킷 경우 0)

---

## Step 3 — IOCP Send: SendBufferPool 싱글턴 도입 ✅

**신규 파일**: `Server/ServerEngine/Network/Core/SendBufferPool.h/.cpp`

RIO 경로는 이미 slab 최적화 완료. IOCP 전송 경로의 per-send 힙 할당을 제거한다.

### 변경 전 (메시지당 1 alloc + 2 memcpy)

```cpp
// Send(): copy 1 — 큐에 넣기 위한 복사
std::vector<char> buf(data, data + size);  // alloc + memcpy
mSendQueue.push(std::move(buf));

// PostSend(): copy 2 — IOCP 버퍼로 복사
memcpy(mSendContext.buffer, front.data(), front.size());
```

### 변경 후 (0 alloc + 1 memcpy, zero-copy WSASend)

`SendBufferPool` 설계:
```cpp
class SendBufferPool {
public:
    static SendBufferPool& Instance();
    void Initialize(size_t poolSize, size_t slotSize);
    struct Slot { char* ptr; size_t idx; };
    Slot   Acquire();          // O(1) 프리리스트 팝
    void   Release(size_t);    // O(1) 프리리스트 푸시
    char*  SlotPtr(size_t);    // 슬롯 포인터 직접 조회
private:
    std::vector<char>   mStorage;   // poolSize × slotSize 연속 메모리
    std::vector<size_t> mFreeSlots;
    std::mutex          mMutex;
    size_t              mSlotSize{0};
};
```

`Session::Send()` IOCP 경로:
```cpp
auto slot = SendBufferPool::Instance().Acquire();  // O(1)
std::memcpy(slot.ptr, data, size);                 // 1회 복사
mSendQueue.push({slot.idx, (uint32_t)size});       // alloc 없음
```

`Session::PostSend()` IOCP 경로 — zero-copy WSASend:
```cpp
char* slotPtr = SendBufferPool::Instance().SlotPtr(req.slotIdx);
mSendContext.wsaBuf.buf = slotPtr;  // 포인터 직접 설정, memcpy 없음
mSendContext.wsaBuf.len = req.size;
```

초기화 (`WindowsNetworkEngine::InitializePlatform()`):
```cpp
if (mMode == Mode::IOCP) {
    Core::SendBufferPool::Instance().Initialize(
        effectiveMax * 4, Core::SEND_BUFFER_SIZE);
}
```

**효과**: IOCP Send 경로에서 per-message 힙 할당 0, memcpy 2회 → 1회

---

## 2026-03-01 퍼포먼스 테스트 결과

**실행**: `run_perf_test.ps1 -Phase all -RampClients @(10,100,500,1000) -SustainSec 30 -BinMode Release`

| 단계 | 목표 | 실제 | 오류 | Server WS | RTT avg | 판정 |
|------|------|------|------|-----------|---------|------|
| 10   | 10   | 10   | 0    | 178.9 MB  | 1 ms    | **PASS** |
| 100  | 100  | 100  | 0    | 180.6 MB  | 1 ms    | **PASS** |
| 500  | 500  | 500  | 0    | 188 MB    | 0 ms    | **PASS** |
| 1000 | 1000 | 1000 | 0    | 193.7 MB  | 0 ms    | **PASS** |

> 상세 로그: `Doc/Performance/Logs/20260301_111832/`

---

## 전체 최적화 이력 요약

| 날짜 | 최적화 | 효과 |
|------|--------|------|
| 2026-02-06 | SafeQueue move semantics, DBTask rvalue ref, ForEachSession const ref | 복사 제거 |
| 2026-02-16 | ProcessRawRecv O(n)→O(1) 오프셋 기반, mPingSequence atomic | 수신 경로 개선 |
| 2026-02-28 | RIO slab pool (WSA 10055 수정), 1000 클라이언트 PASS | 시스템 자원 한계 돌파 |
| 2026-03-01 | AsyncBufferPool O(1) 프리리스트, ProcessRawRecv 배치 버퍼, SendBufferPool zero-copy | 할당·복사 최소화 |
