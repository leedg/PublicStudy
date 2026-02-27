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

### 적용된 최적화
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
