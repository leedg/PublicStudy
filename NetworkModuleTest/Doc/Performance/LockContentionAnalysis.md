# 🔒 Lock 경합(Lock Contention) 분석 보고서

**날짜**: 2026-02-05 (2026-02-16 Session 수신 버퍼 최적화 + mPingSequence 원자화 반영)
**분석 범위**: NetworkModuleTest 프로젝트 전체
**목적**: Lock 경합 및 Deadlock 위험 식별, 성능 최적화 권장사항 제시

---

## 📋 Executive Summary

### 분석 결과 요약

| 심각도 | 항목 수 | 설명 |
|--------|---------|------|
| 🔴 **High** | 2개 | 즉시 수정 필요 |
| ⚠️ **Medium** | 4개 | 성능 영향 가능, 개선 권장 |
| 💡 **Low** | 3개 | 최적화 가능 영역 |
| ✅ **Good** | 5개 | 적절히 처리됨 |

### 주요 발견사항

1. **SessionManager::CloseAllSessions()** - Deadlock 위험 🔴
2. **DBTaskQueue::GetQueueSize()** - 불필요한 Lock 경합 ⚠️
3. **Session::Send()** - 높은 빈도 Lock 경합 ⚠️
4. **SafeQueue::Push()** - 최적화 가능 💡

### 2026-02-16 적용 완료 최적화

| 항목 | 이전 | 이후 | 효과 |
|------|------|------|------|
| `ProcessRawRecv` TCP 재조립 | O(n) `erase()` 반복 | O(1) `mRecvAccumOffset` 진행 + 주기적 compact | 고빈도 수신 시 패킷당 O(n) 비용 제거 |
| `mPingSequence` | `uint32_t` (비원자) | `std::atomic<uint32_t>` | 핑 타이머 스레드 ↔ IO 스레드 경쟁 조건 해소 |
| `CloseConnection` 이벤트 | 직접 `OnDisconnected()` 호출 | `mLogicThreadPool.Submit()` | 연결 해제 경로 스레드 안전성 통일 |

---

## 🔍 상세 분석

### 1️⃣ DBTaskQueue (새로 추가됨)

**파일**: `Server/TestServer/src/DBTaskQueue.cpp`

#### ✅ **양호한 부분**

```cpp
// Line 184-197: WorkerThreadFunc - 올바른 Lock 패턴
{
    std::unique_lock<std::mutex> lock(mQueueMutex);

    mQueueCV.wait(lock, [this] {
        return !mTaskQueue.empty() || !mIsRunning.load();
    });

    if (!mTaskQueue.empty())
    {
        task = std::move(mTaskQueue.front());
        mTaskQueue.pop();
        hasTask = true;
    }
}
// Lock 범위 밖에서 작업 처리 - 훌륭함!
if (hasTask)
{
    ProcessTask(task);
}
```

**장점**:
- ✅ Lock 범위 최소화 (큐 접근만)
- ✅ 작업 처리는 Lock 외부
- ✅ `std::move()` 사용으로 복사 방지
- ✅ Spurious wakeup 처리 (`!mTaskQueue.empty()` 조건)

#### ⚠️ **개선 필요: GetQueueSize()**

```cpp
// Line 157-161
size_t DBTaskQueue::GetQueueSize() const
{
    std::lock_guard<std::mutex> lock(mQueueMutex);
    return mTaskQueue.size();  // ⚠️ Lock 경합 발생 가능
}
```

**문제점**:
- 통계 조회를 위한 Lock이 작업 큐잉/디큐잉과 경합
- 높은 빈도로 호출 시 성능 저하

**해결 방안**:
```cpp
// 옵션 1: Atomic 카운터 사용
std::atomic<size_t> mQueueSize{0};

void EnqueueTask(DBTask task)
{
    {
        std::lock_guard<std::mutex> lock(mQueueMutex);
        mTaskQueue.push(std::move(task));
        mQueueSize.fetch_add(1, std::memory_order_relaxed);
    }
    mQueueCV.notify_one();
}

size_t GetQueueSize() const
{
    return mQueueSize.load(std::memory_order_relaxed);  // Lock-free!
}

// WorkerThreadFunc에서
if (!mTaskQueue.empty())
{
    task = std::move(mTaskQueue.front());
    mTaskQueue.pop();
    mQueueSize.fetch_sub(1, std::memory_order_relaxed);
    hasTask = true;
}
```

**우선순위**: ⚠️ Medium (통계 조회 빈도가 높을 경우 High)

#### ✅ **양호: Atomic 카운터 사용**

```cpp
// Line 239, 243, 250
mProcessedCount.fetch_add(1);  // ✅ Lock-free, 적절함
mFailedCount.fetch_add(1);
```

**장점**:
- Lock 없이 카운터 증가
- 여러 워커 스레드에서 동시 접근 가능

---

### 2️⃣ SessionManager

**파일**: `Server/ServerEngine/Network/Core/SessionManager.cpp`

#### 🔴 **심각: CloseAllSessions() - Deadlock 위험**

```cpp
// Line 129-140
void SessionManager::CloseAllSessions()
{
    std::lock_guard<std::mutex> lock(mMutex);  // 🔴 Lock 획득

    for (auto &[id, session] : mSessions)
    {
        session->Close();  // 🔴 Session::Close() 호출
    }

    mSessions.clear();
    Utils::Logger::Info("All sessions closed");
}
```

**Deadlock 시나리오**:
```
Thread A (CloseAllSessions):
1. mMutex Lock 획득
2. session->Close() 호출
   └─> Session::FlushSendQueue()
       └─> mSendMutex Lock 시도

Thread B (Session::Send):
1. mSendMutex Lock 획득
2. 작업 중 SessionManager::RemoveSession() 호출됨
   └─> mMutex Lock 시도  // 🔴 DEADLOCK!
```

**해결 방안**:
```cpp
void SessionManager::CloseAllSessions()
{
    // 1. Lock 범위 밖에서 세션 복사
    std::vector<SessionRef> sessionsCopy;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        sessionsCopy.reserve(mSessions.size());
        for (auto &[id, session] : mSessions)
        {
            sessionsCopy.push_back(session);
        }
    }

    // 2. Lock 없이 세션 닫기
    for (auto &session : sessionsCopy)
    {
        session->Close();
    }

    // 3. 다시 Lock 획득하여 정리
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mSessions.clear();
        Utils::Logger::Info("All sessions closed");
    }
}
```

**우선순위**: 🔴 **High - 즉시 수정 필요**

#### ⚠️ **개선 가능: RemoveSession(SessionRef)**

```cpp
// Line 71-86
void SessionManager::RemoveSession(SessionRef session)
{
    if (!session)
    {
        return;
    }

    // 🔴 Lock 없이 Close() 호출
    if (session->IsConnected())
    {
        session->Close();  // Session 내부 Lock
    }

    RemoveSession(session->GetId());  // SessionManager Lock
}
```

**잠재적 문제**:
- `session->Close()` 중에 다른 스레드가 같은 세션에 접근 가능
- Race condition 가능성

**개선안**:
```cpp
void SessionManager::RemoveSession(SessionRef session)
{
    if (!session)
    {
        return;
    }

    Utils::ConnectionId id = session->GetId();

    // Lock 획득 전에 Close
    if (session->IsConnected())
    {
        session->Close();
    }

    // 이후 안전하게 제거
    RemoveSession(id);
}
```

**우선순위**: ⚠️ Medium

#### ✅ **양호: ForEachSession()**

```cpp
// Line 101-121
void SessionManager::ForEachSession(std::function<void(SessionRef)> func)
{
    // Lock 범위 최소화를 위해 복사
    std::vector<SessionRef> sessionsCopy;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        sessionsCopy.reserve(mSessions.size());
        for (auto &[id, session] : mSessions)
        {
            sessionsCopy.push_back(session);
        }
    }

    // Lock 없이 처리
    for (auto &session : sessionsCopy)
    {
        func(session);
    }
}
```

**장점**:
- ✅ Lock 범위 최소화
- ✅ 긴 작업 중 Lock 보유 방지
- ✅ Deadlock 위험 없음

**참고**: 이 패턴을 `CloseAllSessions()`에도 적용해야 함!

---

### 3️⃣ Session

**파일**: `Server/ServerEngine/Network/Core/Session.h`

#### ⚠️ **개선 가능: Send() - 높은 빈도 Lock 경합**

```cpp
// Session.cpp (추정 구현)
void Session::Send(const void *data, uint32_t size)
{
    std::vector<char> packet(size);
    std::memcpy(packet.data(), data, size);

    {
        std::lock_guard<std::mutex> lock(mSendMutex);  // ⚠️ 매 Send 호출마다
        mSendQueue.push(std::move(packet));
    }

    FlushSendQueue();
}
```

**문제점**:
- 게임 서버에서 초당 수천~수만 번 호출 가능
- Lock 경합으로 인한 성능 저하

**개선 방안 1: Lock-Free Queue**
```cpp
// Lock-free SPSC/MPSC 큐 사용
#include <boost/lockfree/queue.hpp>

class Session
{
private:
    boost::lockfree::queue<std::vector<char>*> mSendQueue;
    // Lock 불필요!
};
```

**개선 방안 2: Batch Send**
```cpp
void Session::Send(const void *data, uint32_t size)
{
    // Thread-local 버퍼에 축적
    thread_local std::vector<std::vector<char>> batchBuffer;

    batchBuffer.push_back(std::vector<char>(
        static_cast<const char*>(data),
        static_cast<const char*>(data) + size
    ));

    // 일정 개수 모이면 한 번에 전송
    if (batchBuffer.size() >= BATCH_SIZE)
    {
        std::lock_guard<std::mutex> lock(mSendMutex);
        for (auto& packet : batchBuffer)
        {
            mSendQueue.push(std::move(packet));
        }
        batchBuffer.clear();

        FlushSendQueue();
    }
}
```

**우선순위**: ⚠️ Medium-High (트래픽에 따라)

#### ✅ **양호: Atomic Flag 사용**

```cpp
// Session.h Line 161
std::atomic<bool> mIsSending;  // ✅ Send 중복 방지
```

**장점**:
- Lock-free로 전송 중 상태 확인
- 여러 스레드에서 Send 호출 시 안전

#### ✅ **[2026-02-16 적용] ProcessRawRecv — O(1) 오프셋 기반 TCP 재조립**

**이전 구현 (O(n))**:
```cpp
// 패킷 처리 후 앞부분을 매번 erase → O(n) 비용
mRecvAccumBuffer.erase(
    mRecvAccumBuffer.begin(),
    mRecvAccumBuffer.begin() + packetSize);
```

**현재 구현 (O(1))**:
```cpp
// mRecvAccumOffset을 전진시켜 처리된 데이터를 논리적으로 건너뜀
mRecvAccumOffset += packetSize;

// 버퍼 끝까지 소비되면 O(1) clear
if (mRecvAccumOffset >= mRecvAccumBuffer.size()) {
    mRecvAccumBuffer.clear(); mRecvAccumOffset = 0;
}
// 절반 이상 소비되면 prefix만 erase (상각 O(1))
else if (mRecvAccumOffset > mRecvAccumBuffer.size() / 2) {
    mRecvAccumBuffer.erase(
        mRecvAccumBuffer.begin(),
        mRecvAccumBuffer.begin() + static_cast<std::ptrdiff_t>(mRecvAccumOffset));
    mRecvAccumOffset = 0;
}
```

**장점**:
- ✅ 패킷당 O(n) 메모리 이동 제거
- ✅ 고빈도 소형 패킷 환경에서 대폭 개선
- ✅ `DBRecvLoop`의 offset 전략과 동일한 패턴으로 일관성 확보

#### ✅ **[2026-02-16 적용] mPingSequence — `std::atomic<uint32_t>`**

**이전**: `uint32_t mPingSequence` — 핑 타이머 스레드와 IO 완료 스레드에서 비원자 접근

**이후**: `std::atomic<uint32_t> mPingSequence` — 모든 접근 원자 보장

```cpp
// Session.h
std::atomic<uint32_t> mPingSequence{0};

uint32_t GetPingSequence() const {
    return mPingSequence.load(std::memory_order_acquire);
}
uint32_t IncrementPingSequence() {
    return mPingSequence.fetch_add(1, std::memory_order_acq_rel);
}
```

---

### 4️⃣ SafeQueue

**파일**: `Server/ServerEngine/Utils/SafeQueue.h`

#### 💡 **최적화 가능: Push() notify 위치**

```cpp
// Line 24-33
void Push(const T &item)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mQueue.push(item);
    }
    // ✅ Lock 밖에서 notify - 좋음!
    mCondition.notify_one();
}
```

**현재 구현**: ✅ 이미 최적화됨
- Lock 밖에서 notify
- Thundering herd 방지

**추가 개선 가능**:
```cpp
// Move semantics 지원
void Push(T&& item)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mQueue.push(std::move(item));  // 복사 대신 이동
    }
    mCondition.notify_one();
}

// Emplace 지원
template<typename... Args>
void Emplace(Args&&... args)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mQueue.emplace(std::forward<Args>(args)...);
    }
    mCondition.notify_one();
}
```

**우선순위**: 💡 Low (성능 향상 가능)

#### ✅ **양호: Spurious Wakeup 처리**

```cpp
// Line 48
mCondition.wait(lock, [this] { return !mQueue.empty() || mShutdown; });
```

**장점**:
- ✅ Predicate 사용으로 spurious wakeup 방지
- ✅ Shutdown 시그널 처리

---

### 5️⃣ NetworkEngine (Windows 경로)

**파일**: `Server/ServerEngine/Network/Platforms/WindowsNetworkEngine.cpp`,  
`Server/ServerEngine/Network/Core/BaseNetworkEngine.cpp`

#### ✅ **양호: IOCP/RIO 완료 처리**

Windows 경로는 IOCP/RIO 기반으로 완료 처리를 수행하며, 대부분의 동기화가 커널 레벨에서 처리됩니다.

**장점**:
- ✅ User-level Lock 최소화
- ✅ Kernel-level 동기화 (매우 효율적)
- ✅ Scalable (수만 연결 지원)

**주의사항**:
- SessionManager와의 상호작용 시 Lock 순서 주의 필요
- Session 생성/제거 시 경합 가능

---

## 📊 Lock 경합 위험도 매트릭스

| 컴포넌트 | Lock 빈도 | Lock 지속시간 | 경합 위험도 | 우선순위 |
|----------|-----------|---------------|-------------|----------|
| **SessionManager::CloseAllSessions** | Low | Long | 🔴 High | P0 |
| **Session::Send** | Very High | Short | ⚠️ Medium-High | P1 |
| **DBTaskQueue::GetQueueSize** | Medium | Very Short | ⚠️ Medium | P2 |
| **SessionManager::RemoveSession** | Medium | Medium | ⚠️ Medium | P2 |
| **SafeQueue::Push** | High | Very Short | 💡 Low | P3 |
| **DBTaskQueue::EnqueueTask** | High | Very Short | ✅ Good | - |
| **SessionManager::ForEachSession** | Low | Short | ✅ Good | - |

---

## 🎯 우선순위별 개선 권장사항

### 🔴 **P0 - 즉시 수정 (Deadlock 위험)**

#### 1. SessionManager::CloseAllSessions()

**현재 코드**:
```cpp
void SessionManager::CloseAllSessions()
{
    std::lock_guard<std::mutex> lock(mMutex);  // 위험!
    for (auto &[id, session] : mSessions)
    {
        session->Close();
    }
    mSessions.clear();
}
```

**수정 코드**:
```cpp
void SessionManager::CloseAllSessions()
{
    std::vector<SessionRef> sessionsCopy;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        sessionsCopy.reserve(mSessions.size());
        for (auto &[id, session] : mSessions)
        {
            sessionsCopy.push_back(session);
        }
    }

    for (auto &session : sessionsCopy)
    {
        session->Close();
    }

    {
        std::lock_guard<std::mutex> lock(mMutex);
        mSessions.clear();
    }
}
```

---

### ⚠️ **P1 - 높은 우선순위 (성능 영향)**

#### 2. Session::Send() - Lock-Free 또는 Batch 처리

**옵션 A: Lock-Free Queue (권장)**
```cpp
#include <boost/lockfree/queue.hpp>

class Session
{
private:
    struct SendPacket
    {
        std::vector<char> data;
    };

    boost::lockfree::queue<SendPacket*> mSendQueue{128};
    std::atomic<bool> mIsSending{false};

public:
    void Send(const void *data, uint32_t size)
    {
        auto* packet = new SendPacket{
            std::vector<char>(
                static_cast<const char*>(data),
                static_cast<const char*>(data) + size
            )
        };

        while (!mSendQueue.push(packet))
        {
            // 큐가 가득 찬 경우 재시도 또는 에러 처리
            std::this_thread::yield();
        }

        FlushSendQueue();
    }
};
```

**옵션 B: Batch Send**
```cpp
void Session::SendBatch(const std::vector<std::pair<const void*, uint32_t>>& packets)
{
    std::lock_guard<std::mutex> lock(mSendMutex);

    for (const auto& [data, size] : packets)
    {
        std::vector<char> packet(size);
        std::memcpy(packet.data(), data, size);
        mSendQueue.push(std::move(packet));
    }

    FlushSendQueue();
}
```

---

### ⚠️ **P2 - 중간 우선순위 (최적화 권장)**

#### 3. DBTaskQueue::GetQueueSize() - Atomic 카운터

**수정 코드**:
```cpp
class DBTaskQueue
{
private:
    std::queue<DBTask> mTaskQueue;
    std::atomic<size_t> mQueueSize{0};  // 추가
    mutable std::mutex mQueueMutex;
    std::condition_variable mQueueCV;

public:
    void EnqueueTask(DBTask task)
    {
        if (!mIsRunning.load())
        {
            // ...
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            mTaskQueue.push(std::move(task));
            mQueueSize.fetch_add(1, std::memory_order_relaxed);  // 추가
        }

        mQueueCV.notify_one();
    }

    size_t GetQueueSize() const
    {
        return mQueueSize.load(std::memory_order_relaxed);  // Lock-free!
    }

private:
    void WorkerThreadFunc()
    {
        // ...
        {
            std::unique_lock<std::mutex> lock(mQueueMutex);

            mQueueCV.wait(lock, [this] {
                return !mTaskQueue.empty() || !mIsRunning.load();
            });

            if (!mTaskQueue.empty())
            {
                task = std::move(mTaskQueue.front());
                mTaskQueue.pop();
                mQueueSize.fetch_sub(1, std::memory_order_relaxed);  // 추가
                hasTask = true;
            }
        }
        // ...
    }
};
```

#### 4. SessionManager::RemoveSession(SessionRef) - 안전성 개선

**수정 코드**:
```cpp
void SessionManager::RemoveSession(SessionRef session)
{
    if (!session)
    {
        return;
    }

    Utils::ConnectionId id = session->GetId();

    // Close 먼저 (Lock 없이)
    if (session->IsConnected())
    {
        session->Close();
    }

    // 이후 안전하게 제거
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mSessions.find(id);
        if (it != mSessions.end())
        {
            mSessions.erase(it);
        }
    }
}
```

---

### 💡 **P3 - 낮은 우선순위 (성능 향상)**

#### 5. SafeQueue - Move Semantics 지원

**추가 코드**:
```cpp
template <typename T>
class SafeQueue
{
public:
    // 기존 Push (복사)
    void Push(const T &item) { /* ... */ }

    // Move Push 추가
    void Push(T&& item)
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mQueue.push(std::move(item));
        }
        mCondition.notify_one();
    }

    // Emplace 추가
    template<typename... Args>
    void Emplace(Args&&... args)
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mQueue.emplace(std::forward<Args>(args)...);
        }
        mCondition.notify_one();
    }
};
```

---

## 🔧 추가 권장사항

### 1. **Reader-Writer Lock 고려**

읽기가 많고 쓰기가 적은 경우:

```cpp
#include <shared_mutex>

class SessionManager
{
private:
    mutable std::shared_mutex mMutex;  // shared_mutex로 변경

public:
    SessionRef GetSession(Utils::ConnectionId id)
    {
        std::shared_lock<std::shared_mutex> lock(mMutex);  // 읽기 Lock
        auto it = mSessions.find(id);
        if (it != mSessions.end())
        {
            return it->second;
        }
        return nullptr;
    }

    void RemoveSession(Utils::ConnectionId id)
    {
        std::unique_lock<std::shared_mutex> lock(mMutex);  // 쓰기 Lock
        mSessions.erase(id);
    }
};
```

**적용 대상**:
- SessionManager (읽기 >> 쓰기)
- 설정 관리자
- 라우팅 테이블

---

### 2. **Lock 순서 정의**

**전역 Lock 순서 규칙**:
```
1. SessionManager::mMutex
2. Session::mSendMutex
3. DBTaskQueue::mQueueMutex
```

**문서화**:
```cpp
// SessionManager.h
// Lock Order: This class's mMutex must be acquired BEFORE Session::mSendMutex
class SessionManager { /* ... */ };
```

---

### 3. **Lock-Free 자료구조 도입**

**추천 라이브러리**:
- Boost.Lockfree
- Folly (Facebook)
- libcds (Concurrent Data Structures)

**적용 고려 대상**:
- Session Send Queue (초고빈도)
- Packet Pool (메모리 할당)
- 통계 카운터

---

## 📈 성능 측정 권장사항

### 1. **Lock Profiling**

```cpp
// Lock 대기 시간 측정
class ProfiledMutex
{
private:
    std::mutex mMutex;
    std::atomic<uint64_t> mContentionCount{0};
    std::atomic<uint64_t> mTotalWaitTimeNs{0};

public:
    void lock()
    {
        auto start = std::chrono::high_resolution_clock::now();

        bool acquired = mMutex.try_lock();
        if (!acquired)
        {
            mContentionCount.fetch_add(1);
            mMutex.lock();

            auto end = std::chrono::high_resolution_clock::now();
            auto waitTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - start).count();
            mTotalWaitTimeNs.fetch_add(waitTime);
        }
    }

    void unlock()
    {
        mMutex.unlock();
    }

    void PrintStats()
    {
        uint64_t count = mContentionCount.load();
        uint64_t totalWait = mTotalWaitTimeNs.load();

        if (count > 0)
        {
            Logger::Info("Lock contention: " + std::to_string(count) +
                        " times, Avg wait: " +
                        std::to_string(totalWait / count) + " ns");
        }
    }
};
```

### 2. **벤치마크 시나리오**

```cpp
// 1000명 동시 접속, 초당 10,000 패킷 전송
void BenchmarkSessionManager()
{
    const size_t CLIENT_COUNT = 1000;
    const size_t PACKETS_PER_SEC = 10000;

    // 측정 시작
    auto start = std::chrono::high_resolution_clock::now();

    // 부하 생성
    // ...

    // 측정 종료
    auto end = std::chrono::high_resolution_clock::now();

    // Lock 통계 출력
    SessionManager::Instance().PrintLockStats();
}
```

---

## ✅ 체크리스트

### 즉시 수정 필요 (P0)
- [ ] SessionManager::CloseAllSessions() Deadlock 수정

### 높은 우선순위 (P1)
- [ ] Session::Send() Lock-Free Queue 또는 Batch 처리

### 중간 우선순위 (P2)
- [ ] DBTaskQueue::GetQueueSize() Atomic 카운터
- [ ] SessionManager::RemoveSession(SessionRef) 안전성 개선

### 낮은 우선순위 (P3)
- [ ] SafeQueue Move Semantics 지원
- [ ] Reader-Writer Lock 도입 검토

### 모니터링 및 테스트
- [ ] Lock Profiling 도구 구현
- [ ] 부하 테스트 시나리오 작성
- [ ] Lock 순서 문서화

---

## 🎯 결론

### 현재 상태 평가

**강점**:
- ✅ DBTaskQueue는 잘 설계됨 (Lock 범위 최소화)
- ✅ SessionManager::ForEachSession()의 복사 패턴 우수
- ✅ Atomic 연산 적절히 사용됨

**약점**:
- 🔴 SessionManager::CloseAllSessions() Deadlock 위험
- ⚠️ Session::Send() 고빈도 Lock 경합
- ⚠️ 일부 Lock 순서 규칙 미정의

### 권장 조치

1. **즉시 수정** (이번 주 내)
   - SessionManager::CloseAllSessions() 수정

2. **단기 개선** (1-2주 내)
   - Session::Send() 최적화
   - DBTaskQueue::GetQueueSize() Atomic 전환

3. **중기 개선** (1개월 내)
   - Lock Profiling 도구 구현
   - 성능 벤치마크 수행
   - Reader-Writer Lock 도입 검토

4. **장기 개선** (필요 시)
   - Lock-Free 자료구조 전면 도입
   - Folly/Boost.Lockfree 라이브러리 사용

---

**다음 단계**: P0 항목부터 순차적으로 수정 후 성능 테스트 수행 권장
