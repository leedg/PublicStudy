# Architecture Decision: Session Class Naming Strategy

**작성일**: 2026-01-27  
**주제**: IocpObjectSession vs RIOObjectSession vs 공용 AsyncObjectSession  
**영향도**: 아키텍처 수준 (클래스 설계, API, 마이그레이션 경로)  
**상태**: 의사결정 필요

---

## 🎯 핵심 질문

**"IocpObjectSession과 같은 경우에는 RIOObjectSession이 명시적인가?  
아니면 공용으로 사용할 수 있는 새로운 네이밍이 좋은가?"**

---

## 📊 옵션 비교

### **Option A: 플랫폼 명시적 (Platform-Explicit)**

```cpp
// Windows IOCP용
class IocpObjectSession : public IocpObject
{
    void HandleIocp(LPOVERLAPPED overlapped, DWORD transferred, DWORD error);
};

// Windows RIO용
class RIOObjectSession : public IocpObject
{
    void HandleRIO(const CompletionResult& result);
};

// Linux io_uring용
class IOUringObjectSession : public IocpObject
{
    void HandleIOUring(const io_uring_cqe* cqe);
};

// macOS kqueue용
class KqueueObjectSession : public IocpObject
{
    void HandleKqueue(const struct kevent& event);
};
```

**장점**:
- ✅ **명확성**: 어떤 백엔드인지 한눈에 파악
- ✅ **명시적**: IDE 자동완성 시 옵션 명확
- ✅ **디버깅**: 런타임 타입 확인 용이
- ✅ **백엔드 특화**: 각 백엔드 최적화 가능

**단점**:
- ❌ **코드 중복**: 4개 클래스가 거의 동일
- ❌ **유지보수**: 공통 기능 수정 시 4곳 모두 변경
- ❌ **학습곡선**: 신규 개발자가 4개 클래스 모두 이해 필요
- ❌ **확장성**: 새 플랫폼 추가 시 새 클래스 필요

---

### **Option B: 공용 명칭 (Platform-Agnostic)**

```cpp
// 플랫폼 무관한 통일 이름
class AsyncObjectSession : public AsyncIOObject
{
public:
    // 플랫폼 독립적 인터페이스
    virtual uint32_t SendAsync(const void* buffer, uint32_t length) = 0;
    virtual uint32_t RecvAsync(void* buffer, uint32_t maxLength) = 0;
    
    // 콜백 (플랫폼별로 다르지만 외부에는 통일)
    virtual void OnCompletion(const CompletionResult& result) = 0;
};

// 플랫폼별 구현은 내부
namespace Windows
{
    class IocpAsyncObjectSession : public AsyncObjectSession { };
    class RIOAsyncObjectSession : public AsyncObjectSession { };
}

namespace Linux
{
    class IOUringAsyncObjectSession : public AsyncObjectSession { };
}

namespace MacOS
{
    class KqueueAsyncObjectSession : public AsyncObjectSession { };
}
```

**장점**:
- ✅ **간결성**: 애플리케이션은 `AsyncObjectSession` 하나만 사용
- ✅ **유지보수**: 공통 로직 한 곳에 집중
- ✅ **확장성**: 새 플랫폼 추가 시 기존 코드 변경 없음
- ✅ **학습곡선**: 신규 개발자가 하나의 인터페이스만 학습

**단점**:
- ❌ **추상화 오버헤드**: 가상 함수 호출 증가
- ❌ **명확성 저하**: 어떤 백엔드인지 불명확
- ❌ **디버깅**: `dynamic_cast` 필요
- ❌ **백엔드 특화**: 최적화하기 어려움

---

### **Option C: 하이브리드 (Best of Both)**

```cpp
// 공용 기본 클래스 (플랫폼 무관)
class AsyncObjectSession
{
public:
    virtual ~AsyncObjectSession() = default;
    
    // 플랫폼 무관 인터페이스
    virtual uint32_t SendAsync(const void* buffer, uint32_t length) = 0;
    virtual uint32_t RecvAsync(void* buffer, uint32_t maxLength) = 0;
    virtual void OnCompletion(const CompletionResult& result) = 0;
    
    // 플랫폼 식별
    virtual const char* GetPlatformName() const = 0;
};

// 플랫폼별 구체 구현 (명시적 이름)
class IocpObjectSession : public AsyncObjectSession
{
private:
    // IOCP 특화 로직
    void HandleIocp(LPOVERLAPPED overlapped, DWORD transferred);
public:
    const char* GetPlatformName() const override { return "IOCP"; }
};

class RIOObjectSession : public AsyncObjectSession
{
private:
    // RIO 특화 로직
    void HandleRIO(const RIO_CQ_ENTRY& entry);
public:
    const char* GetPlatformName() const override { return "RIO"; }
};

// 팩토리 (플랫폼별 자동 선택)
std::unique_ptr<AsyncObjectSession> CreateAsyncObjectSession()
{
    if constexpr (PLATFORM_WINDOWS)
    {
        if (IsRIOAvailable())
            return std::make_unique<RIOObjectSession>();
        return std::make_unique<IocpObjectSession>();
    }
    else if constexpr (PLATFORM_LINUX)
    {
        if (HasIOUring())
            return std::make_unique<IOUringObjectSession>();
        return std::make_unique<EpollObjectSession>();
    }
}
```

**장점**:
- ✅ **명확성**: 플랫폼별 명시적 이름 유지
- ✅ **간결성**: 애플리케이션은 `AsyncObjectSession` 인터페이스 사용
- ✅ **확장성**: 새 플랫폼 추가 시 쉬움
- ✅ **디버깅**: `GetPlatformName()` 으로 확인 가능
- ✅ **유지보수**: 공통 기능은 기본 클래스에서 관리
- ✅ **유연성**: 플랫폼 특화 최적화도 가능

**단점**:
- ⚠️ **약간의 복잡성**: 상속 계층 구조 있음 (미미함)

---

## 🔍 세부 비교 분석

### 시나리오 1: 공통 기능 추가

**요구사항**: 모든 세션에 "타임아웃 처리" 기능 추가

#### Option A (플랫폼 명시)
```cpp
// ❌ 4개 클래스 모두 수정 필요
class IocpObjectSession : public IocpObject {
    void HandleTimeout() { /* ... */ }  // 수정
};
class RIOObjectSession : public IocpObject {
    void HandleTimeout() { /* ... */ }  // 수정
};
class IOUringObjectSession : public IocpObject {
    void HandleTimeout() { /* ... */ }  // 수정
};
// ... 등

// 코드 리뷰: 4회
// 테스트: 4개 플랫폼 각각
// 누락 위험: 한 개 빠뜨릴 수 있음
```

#### Option B (공용)
```cpp
// ✅ 한 곳만 수정
class AsyncObjectSession {
    void HandleTimeout() { /* ... */ }  // 기본 구현
};

// 코드 리뷰: 1회
// 테스트: 자동으로 모든 플랫폼에 적용
```

#### Option C (하이브리드)
```cpp
// ✅ 기본 클래스에 구현
class AsyncObjectSession {
    void HandleTimeout() { /* ... */ }  // 기본 구현
};

// 필요시 플랫폼별 오버라이드
class RIOObjectSession : public AsyncObjectSession {
    void HandleTimeout() override {
        // RIO 특화 처리
    }
};
```

**승자**: **Option C** (최고의 유연성)

---

### 시나리오 2: 새 플랫폼 추가 (예: eBPF)

#### Option A
```cpp
// ❌ 새 클래스 필요
class EBPFObjectSession : public IocpObject { };

// 기존 애플리케이션 코드도 수정 필요
if (session is IocpObjectSession) { }
else if (session is RIOObjectSession) { }
else if (session is IOUringObjectSession) { }
else if (session is EBPFObjectSession) { }  // 새로 추가
```

#### Option B
```cpp
// ✅ 팩토리만 수정
std::unique_ptr<AsyncObjectSession> factory() {
    // ...
    if (HasEBPF())
        return std::make_unique<EBPFAsyncObjectSession>();
}

// 애플리케이션 코드는 변경 없음!
std::unique_ptr<AsyncObjectSession> session = factory();
```

#### Option C
```cpp
// ✅ Option B와 동일
// 팩토리만 수정, 기존 코드는 변경 없음

class EBPFObjectSession : public AsyncObjectSession { };
```

**승자**: **Option B, C** (기존 코드 변경 최소)

---

### 시나리오 3: 디버깅 시 어떤 백엔드인지 확인

#### Option A
```cpp
// 명시적 (좋음)
if (auto* iocp = dynamic_cast<IocpObjectSession*>(session)) {
    std::cout << "IOCP 세션\n";
}
```

#### Option B
```cpp
// 불명확 (나쁨)
std::cout << typeid(*session).name() << "\n";  // AsyncObjectSession 출력
// "AsyncObjectSession"만 보임, 어떤 구현인지 몰라
```

#### Option C
```cpp
// 명시적 (최고)
std::cout << session->GetPlatformName() << "\n";  // "RIO", "IOCP", 등
```

**승자**: **Option C** (명확하고 일관적)

---

### 시나리오 4: RIO 특화 기능 사용 (예: 버퍼 등록)

#### Option A
```cpp
// 명시적 (좋음)
if (auto* rio = dynamic_cast<RIOObjectSession*>(session)) {
    rio->RegisterBuffer(buffer, size);
}
```

#### Option B
```cpp
// 불가능 또는 복잡 (나쁨)
// Option B는 인터페이스가 플랫폼 무관이므로 RIO 특화 기능 불가

// 억지로 하려면:
if (strcmp(session->GetPlatformName(), "RIO") == 0) {
    auto* rio = static_cast<RIOAsyncObjectSession*>(session);
    rio->RegisterBuffer(buffer, size);
}
```

#### Option C
```cpp
// 선택적 (좋음)
if (auto* rio = dynamic_cast<RIOObjectSession*>(session)) {
    rio->RegisterBuffer(buffer, size);  // RIO 특화 기능
}
```

**승자**: **Option A, C**

---

## 📈 현재 RAON 코드베이스 분석

기존 RAON 구조를 보면:

```
IocpCore
  │
  ├─ IocpObjectSession   (현재)
  │   ├─ mRefCount
  │   ├─ mSocket
  │   ├─ HandleIocp()
  │   └─ Send(), Recv()
  │
  └─ ServiceCoordinator
      └─ AcquireSession()  // IocpObjectSession 반환
```

**현재 특징**:
1. `ServiceCoordinator::AcquireSession()`이 `IocpObjectSession*` 반환
2. `IocpCore::HandleIocp()`가 `IocpObjectSession::HandleIocp()` 호출
3. 세션 풀이 `IocpObjectSession` 타입으로 정렬

**마이그레이션 영향**:
- Option A: ServiceCoordinator 수정 필요 (플랫폼별로)
- Option B: ServiceCoordinator 변경 최소
- Option C: ServiceCoordinator 거의 변경 없음 (팩토리 추가만)

---

## 🏆 **최종 권장사항**

### 추천: **Option C (하이브리드 패턴)**

#### 이유

| 측면 | 평가 |
|------|------|
| **기존 코드 호환성** | ✅ 최고 (IocpObjectSession 유지) |
| **새 플랫폼 확장성** | ✅ 최고 (기존 코드 변경 없음) |
| **명확성** | ✅ 최고 (플랫폼 명시적 이름) |
| **유지보수성** | ✅ 최고 (공통 기능 한 곳에) |
| **디버깅 용이성** | ✅ 최고 (GetPlatformName 메서드) |
| **플랫폼 특화 최적화** | ✅ 가능 |
| **성능 오버헤드** | ✅ 미미 (가상 함수 1-2개) |

#### 구조

```
공용 인터페이스:
    AsyncObjectSession (기본 클래스)
        ├─ SendAsync() / RecvAsync() [공용]
        ├─ OnCompletion() [가상]
        └─ GetPlatformName() [가상]
             │
             ├─ 공통 구현
             │   ├─ RefCount 관리
             │   ├─ Buffer 관리
             │   ├─ Error handling
             │   └─ Timeout 처리
             │
             └─ 플랫폼별 구체 구현
                 ├─ IocpObjectSession
                 ├─ RIOObjectSession
                 ├─ IOUringObjectSession
                 └─ EpollObjectSession
```

---

## 💻 구현 전략

### Phase 1: 현재 상태 유지 (호환성)
```cpp
// 기존 RAON 코드 유지
class IocpObjectSession : public IocpObject { };
```

### Phase 2: 기본 클래스 도입
```cpp
// 새로운 기본 클래스 추가
class AsyncObjectSession {
    virtual ~AsyncObjectSession() = default;
    virtual uint32_t SendAsync(...) = 0;
    virtual void OnCompletion(...) = 0;
    virtual const char* GetPlatformName() const = 0;
    
    // 공통 구현
    void HandleTimeout() { /* ... */ }
};

// 기존 클래스 상속 확장
class IocpObjectSession : public AsyncObjectSession, public IocpObject { };
```

### Phase 3: 다른 플랫폼 추가
```cpp
class RIOObjectSession : public AsyncObjectSession { };
class IOUringObjectSession : public AsyncObjectSession { };
```

### Phase 4: 팩토리 도입
```cpp
std::unique_ptr<AsyncObjectSession> CreateAsyncSession(
    const InetAddress& addr)
{
    if constexpr (PLATFORM_WINDOWS) {
        if (IsRIOAvailable())
            return std::make_unique<RIOObjectSession>(addr);
        return std::make_unique<IocpObjectSession>(addr);
    }
    // ...
}
```

---

## 📝 구현 가이드 (근거)

### 클래스 명칭 결정

```cpp
// ✅ 추천
class IocpObjectSession : public AsyncObjectSession { }
class RIOObjectSession : public AsyncObjectSession { }
class IOUringObjectSession : public AsyncObjectSession { }

// ❌ 피할 것
class ObjectSession { }  // 너무 모호
class NetworkSession { }  // 이미 있는 것 같음
class IocpObjectSession { }  // 기본 클래스 없음
```

### 명칭 규칙

```
AsyncObjectSession          (추상 기본 클래스)
    │
    ├─ Iocp 로 시작        IocpObjectSession
    ├─ RIO 로 시작         RIOObjectSession
    ├─ IOUring 로 시작     IOUringObjectSession
    ├─ Epoll 로 시작       EpollObjectSession
    └─ Kqueue 로 시작      KqueueObjectSession
```

---

## 🔧 ServiceCoordinator 영향도

### 현재 (RAON)
```cpp
class ServiceCoordinator {
    IocpObjectSession* AcquireSession() {
        return pool.Pop();  // IocpObjectSession*
    }
};
```

### 새로운 (Option C)
```cpp
class ServiceCoordinator {
    AsyncObjectSession* AcquireSession() {
        // 팩토리에서 자동 선택된 세션 반환
        return sessionFactory->CreateSession();
    }
};
```

**영향**: 최소 (기본 클래스 타입만 변경)

---

## ✅ 최종 체크리스트

### 의사결정 항목

- [ ] **Option C 채택 동의**: IocpObjectSession은 명시적, AsyncObjectSession은 공용
- [ ] **기본 클래스 추가**: AsyncObjectSession 클래스 생성
- [ ] **메서드 명칭**:
  - [ ] 공용: `SendAsync()`, `RecvAsync()`, `OnCompletion()`
  - [ ] 식별: `GetPlatformName()` 메서드 추가
- [ ] **팩토리 패턴**: 플랫폼별 자동 선택 메커니즘
- [ ] **마이그레이션 경로**:
  - Phase 1: 기본 클래스 추가 (IocpObjectSession 유지)
  - Phase 2: RIO 클래스 추가
  - Phase 3: Linux 클래스 추가
  - Phase 4: 팩토리 통합

---

## 📚 문서 업데이트 필요

이 의사결정을 반영하여 다음 문서 업데이트 필요:

- [ ] 06_Cross_Platform_Architecture.md: "세션 클래스 설계" 섹션 추가
- [ ] 07_API_Design_Document.md: AsyncObjectSession 인터페이스 정의
- [ ] 03_Implementation_Roadmap.md: 클래스 다이어그램 추가

---

**결론**: Option C (하이브리드) 채택 강력 권장

명시적인 플랫폼 클래스 + 공용 기본 인터페이스 = 최고의 설계

