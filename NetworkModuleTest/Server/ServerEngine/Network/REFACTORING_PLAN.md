# NetworkEngine Refactoring Plan
# NetworkEngine 리팩토링 계획

## Current Problem / 현재 문제점

### Inconsistent Architecture / 일관성 없는 아키텍처

```
현재:
- IOCPNetworkEngine: 완전한 서버 엔진 (Session + IOCP 직접 사용)
- AsyncIOProvider: 저수준 I/O 추상화 (5개 플랫폼 구현체)
- 두 레이어가 중복된 IOCP 구현 보유
```

### Design Mismatch / 설계 불일치

1. **IOCPNetworkEngine**: Windows 전용, Session 강결합
2. **AsyncIOProvider**: 크로스 플랫폼, Session 독립

→ **두 목적이 혼재되어 확장성 저하**

---

## Proposed Solution / 제안하는 해결책

### Unified Architecture / 통일된 아키텍처

```
INetworkEngine (추상 인터페이스)
├── WindowsNetworkEngine
│   ├── IOCP 모드 (기본)
│   └── RIO 모드 (고성능)
│
├── LinuxNetworkEngine
│   ├── epoll 모드 (기본)
│   └── io_uring 모드 (고성능)
│
└── macOSNetworkEngine
    └── kqueue 모드

각 Engine이 내부적으로:
- Session 관리 (공통)
- 플랫폼별 AsyncIOProvider 사용 (변경)
```

---

## Implementation Steps / 구현 단계

### Step 1: Create Base NetworkEngine / 기본 NetworkEngine 생성

**BaseNetworkEngine.h/cpp** (공통 로직)
```cpp
class BaseNetworkEngine : public INetworkEngine
{
protected:
    // Common Session management
    SessionManager& mSessionManager;

    // Platform-specific provider
    std::unique_ptr<AsyncIO::AsyncIOProvider> mProvider;

    // Common event system
    std::unordered_map<NetworkEvent, NetworkEventCallback> mCallbacks;

    // Common thread pool
    Utils::ThreadPool mLogicThreadPool;

    // Common statistics
    Statistics mStats;

public:
    // INetworkEngine interface implementation
    bool Initialize(size_t maxConnections, uint16_t port) override;
    bool Start() override;
    void Stop() override;
    // ...

protected:
    // Platform-specific hooks (pure virtual)
    virtual bool InitializePlatform() = 0;
    virtual bool CreateListenSocket() = 0;
    virtual void AcceptLoop() = 0;
    virtual void ProcessCompletions() = 0;
};
```

### Step 2: Platform-Specific Engines / 플랫폼별 엔진

**WindowsNetworkEngine.h/cpp**
```cpp
class WindowsNetworkEngine : public BaseNetworkEngine
{
public:
    enum class Mode { IOCP, RIO };

    explicit WindowsNetworkEngine(Mode mode = Mode::IOCP);

protected:
    bool InitializePlatform() override;
    void AcceptLoop() override;
    void ProcessCompletions() override;

private:
    Mode mMode;
    HANDLE mListenSocket;
};
```

**LinuxNetworkEngine.h/cpp**
```cpp
class LinuxNetworkEngine : public BaseNetworkEngine
{
public:
    enum class Mode { Epoll, IOUring };

    explicit LinuxNetworkEngine(Mode mode = Mode::Epoll);

protected:
    bool InitializePlatform() override;
    void AcceptLoop() override;
    void ProcessCompletions() override;

private:
    Mode mMode;
    int mListenSocket;
};
```

**macOSNetworkEngine.h/cpp**
```cpp
class macOSNetworkEngine : public BaseNetworkEngine
{
protected:
    bool InitializePlatform() override;
    void AcceptLoop() override;
    void ProcessCompletions() override;

private:
    int mListenSocket;
    int mKqueueFd;
};
```

### Step 3: Factory Pattern / 팩토리 패턴

**NetworkEngine.cpp**
```cpp
std::unique_ptr<INetworkEngine> CreateNetworkEngine(const std::string& type)
{
#ifdef _WIN32
    if (type == "rio")
        return std::make_unique<WindowsNetworkEngine>(WindowsNetworkEngine::Mode::RIO);
    else
        return std::make_unique<WindowsNetworkEngine>(WindowsNetworkEngine::Mode::IOCP);

#elif defined(__linux__)
    if (type == "io_uring")
        return std::make_unique<LinuxNetworkEngine>(LinuxNetworkEngine::Mode::IOUring);
    else
        return std::make_unique<LinuxNetworkEngine>(LinuxNetworkEngine::Mode::Epoll);

#elif defined(__APPLE__)
    return std::make_unique<macOSNetworkEngine>();

#else
    return nullptr;
#endif
}
```

### Step 4: Session Integration / Session 통합

**BaseNetworkEngine::ProcessCompletions()**
```cpp
void BaseNetworkEngine::ProcessCompletions()
{
    AsyncIO::CompletionEntry entries[64];
    int count = mProvider->ProcessCompletions(entries, 64, 100);

    for (int i = 0; i < count; ++i)
    {
        auto& entry = entries[i];

        // ConnectionId는 RequestContext에 저장
        ConnectionId connId = static_cast<ConnectionId>(entry.mContext);
        auto session = mSessionManager.GetSession(connId);

        if (!session)
            continue;

        switch (entry.mType)
        {
        case AsyncIOType::Recv:
            ProcessRecvCompletion(session, entry.mResult);
            break;

        case AsyncIOType::Send:
            ProcessSendCompletion(session, entry.mResult);
            break;
        }
    }
}
```

---

## Benefits / 장점

### 1. Unified Interface / 통일된 인터페이스
```cpp
// Before: Windows-only
auto engine = std::make_unique<IOCPNetworkEngine>();

// After: Cross-platform
auto engine = CreateNetworkEngine("auto"); // Platform auto-detect
```

### 2. Runtime Backend Switching / 런타임 백엔드 전환
```cpp
// Windows에서 성능 테스트
auto iocpEngine = CreateNetworkEngine("iocp");
auto rioEngine = CreateNetworkEngine("rio");

// Linux에서 성능 테스트
auto epollEngine = CreateNetworkEngine("epoll");
auto ioUringEngine = CreateNetworkEngine("io_uring");
```

### 3. Code Reuse / 코드 재사용
```
공통 로직 (BaseNetworkEngine):
- Session 관리
- Event 시스템
- Thread pool
- Statistics

플랫폼별 로직 (각 Engine):
- Socket 생성
- Accept 처리
- Completion 처리
```

### 4. Easy Testing / 쉬운 테스트
```cpp
// 모든 플랫폼에서 동일한 테스트 코드
void TestNetworkEngine(INetworkEngine* engine)
{
    engine->Initialize(1000, 8080);
    engine->RegisterEventCallback(NetworkEvent::Connected, ...);
    engine->Start();
    // ...
}
```

---

## Migration Path / 마이그레이션 경로

### Phase 1: Add New Engines (기존 유지) ✅
- BaseNetworkEngine 작성
- WindowsNetworkEngine 작성 (IOCP 모드)
- **기존 IOCPNetworkEngine은 그대로 유지**

### Phase 2: Test & Validate (검증)
- TestServer에서 WindowsNetworkEngine 테스트
- 기존 IOCPNetworkEngine과 동작 비교
- 성능 벤치마크

### Phase 3: Gradual Migration (점진적 전환)
- TestServer → WindowsNetworkEngine
- DBServer → WindowsNetworkEngine
- **안정화 확인 후 IOCPNetworkEngine 제거**

### Phase 4: Add Other Platforms (다른 플랫폼)
- LinuxNetworkEngine 추가
- macOSNetworkEngine 추가

---

## File Structure / 파일 구조

```
Server/ServerEngine/Network/Core/
├── NetworkEngine.h              # INetworkEngine interface
├── BaseNetworkEngine.h/cpp      # Common implementation
├── IOCPNetworkEngine.h/cpp      # Legacy (deprecated)
│
└── Platforms/
    ├── WindowsNetworkEngine.h/cpp
    ├── LinuxNetworkEngine.h/cpp
    └── macOSNetworkEngine.h/cpp
```

---

## Conclusion / 결론

**현재 제안:**
- AsyncIOProvider를 각 플랫폼별 NetworkEngine 내부에 통합
- INetworkEngine 인터페이스 기반 통일
- 기존 IOCPNetworkEngine은 deprecated로 표시, 점진적 제거

**최종 목표:**
```cpp
// Simple, unified, cross-platform
auto engine = CreateNetworkEngine(); // Auto-detect best for platform
engine->Initialize(1000, 8080);
engine->Start();
```

✅ **통일성** - 모든 플랫폼 동일한 인터페이스
✅ **확장성** - 새 플랫폼 추가 용이
✅ **유지보수성** - 공통 로직 한 곳에서 관리
✅ **성능** - 플랫폼별 최적화 가능
