# Network Module Architecture / 네트워크 모듈 아키텍처

## Overview / 개요

This document explains the design philosophy and usage patterns for the network module.
이 문서는 네트워크 모듈의 설계 철학과 사용 패턴을 설명합니다.

## Two-Layer Architecture / 2계층 아키텍처

### Layer 1: High-Level Server Engine (IOCPNetworkEngine)
### 계층 1: 고수준 서버 엔진 (IOCPNetworkEngine)

**Purpose / 목적:**
- Complete server solution with Session management
- Session 관리를 포함한 완전한 서버 솔루션

**Features / 기능:**
- Listen socket management
- Accept thread for incoming connections
- Session lifecycle management (Connect/Disconnect)
- Event-driven callbacks (Connected, Disconnected, DataReceived)
- Thread pool for business logic
- Statistics tracking

**When to Use / 사용 시기:**
- Building a Windows server application
- Need Session-based connection management
- Want integrated event system
- Windows 서버 애플리케이션 구축 시
- Session 기반 연결 관리 필요 시
- 통합 이벤트 시스템 필요 시

**Implementation Details / 구현 상세:**
```cpp
// Session owns IOContext (OVERLAPPED)
struct IOContext : public OVERLAPPED {
    IOType type;
    WSABUF wsaBuf;
    char buffer[RECV_BUFFER_SIZE];
};

// Direct IOCP usage with Session
GetQueuedCompletionStatus(mIOCP, ..., &overlapped, ...);
IOContext* ioContext = static_cast<IOContext*>(overlapped);
// Process based on Session's IOContext
```

---

### Layer 2: Low-Level Async I/O Provider (AsyncIOProvider)
### 계층 2: 저수준 비동기 I/O 공급자 (AsyncIOProvider)

**Purpose / 목적:**
- Platform-independent async I/O abstraction
- 플랫폼 독립적 비동기 I/O 추상화

**Implementations / 구현체:**
- **Windows**: IocpAsyncIOProvider, RIOAsyncIOProvider
- **Linux**: EpollAsyncIOProvider, IOUringAsyncIOProvider
- **macOS**: KqueueAsyncIOProvider

**When to Use / 사용 시기:**
- Building cross-platform network libraries
- Need to switch I/O backends dynamically (IOCP ↔ RIO ↔ epoll)
- Session-independent I/O operations
- Advanced performance optimization scenarios
- 크로스 플랫폼 네트워크 라이브러리 구축 시
- I/O 백엔드를 동적으로 전환해야 할 때
- Session과 독립적인 I/O 작업
- 고급 성능 최적화 시나리오

**Implementation Details / 구현 상세:**
```cpp
// Create provider
auto provider = AsyncIO::CreateAsyncIOProvider(); // Auto-detect platform

// Send data
provider->SendAsync(socket, buffer, size, context, flags);

// Flush requests (for batching backends like RIO)
provider->FlushRequests();

// Process completions
CompletionEntry entries[32];
int count = provider->ProcessCompletions(entries, 32, timeout);

for (int i = 0; i < count; ++i) {
    // Handle completion
    RequestContext ctx = entries[i].mContext;
    int32_t result = entries[i].mResult;
}
```

---

## Design Rationale / 설계 근거

### Why Not Merge Them? / 왜 병합하지 않나요?

**Reason 1: Different Abstraction Levels / 이유 1: 다른 추상화 수준**
- IOCPNetworkEngine: Application-level (Session, Events, Callbacks)
- AsyncIOProvider: System-level (Raw I/O operations)
- IOCPNetworkEngine: 애플리케이션 수준 (Session, 이벤트, 콜백)
- AsyncIOProvider: 시스템 수준 (순수 I/O 작업)

**Reason 2: Different Use Cases / 이유 2: 다른 사용 사례**
- IOCPNetworkEngine: Server applications (TestServer, DBServer)
- AsyncIOProvider: Cross-platform libraries, advanced tools
- IOCPNetworkEngine: 서버 애플리케이션 (TestServer, DBServer)
- AsyncIOProvider: 크로스 플랫폼 라이브러리, 고급 도구

**Reason 3: Performance Optimization / 이유 3: 성능 최적화**
- IOCPNetworkEngine: Optimized for Session-based patterns
  - Session directly owns IOContext (zero overhead)
  - Direct OVERLAPPED pointer access
- AsyncIOProvider: Optimized for flexibility
  - Can switch backends at runtime
  - Platform-independent interface
- IOCPNetworkEngine: Session 기반 패턴에 최적화
  - Session이 IOContext 직접 소유 (오버헤드 없음)
  - OVERLAPPED 포인터 직접 접근
- AsyncIOProvider: 유연성에 최적화
  - 런타임에 백엔드 전환 가능
  - 플랫폼 독립적 인터페이스

---

## Code Organization / 코드 구성

```
Server/ServerEngine/
├── Network/
│   └── Core/
│       ├── IOCPNetworkEngine.h/cpp    # High-level server engine
│       ├── AsyncIOProvider.h          # Low-level I/O interface
│       ├── Session.h/cpp              # Connection session
│       └── SessionManager.h/cpp       # Session pool
│
└── Platforms/
    ├── Windows/
    │   ├── IocpAsyncIOProvider.h/cpp  # IOCP implementation
    │   └── RIOAsyncIOProvider.h/cpp   # RIO implementation
    │
    ├── Linux/
    │   ├── EpollAsyncIOProvider.h/cpp # epoll implementation
    │   └── IOUringAsyncIOProvider.h/cpp # io_uring implementation
    │
    └── macOS/
        └── KqueueAsyncIOProvider.h/cpp # kqueue implementation
```

---

## Usage Examples / 사용 예제

### Example 1: Building a Windows Server / 예제 1: Windows 서버 구축

```cpp
// Use IOCPNetworkEngine for complete server solution
IOCPNetworkEngine engine;

// Register event callbacks
engine.RegisterEventCallback(NetworkEvent::Connected,
    [](ConnectionId id, const uint8_t*, size_t, OSError) {
        Logger::Info("Client connected: " + std::to_string(id));
    });

// Initialize and start
engine.Initialize(1000, 8080);
engine.Start();
```

### Example 2: Cross-Platform Network Library / 예제 2: 크로스 플랫폼 네트워크 라이브러리

```cpp
// Use AsyncIOProvider for platform abstraction
auto provider = AsyncIO::CreateAsyncIOProvider();

// Initialize with queue depth and max concurrent operations
provider->Initialize(1024, 128);

// Send/Recv operations work on all platforms
provider->SendAsync(socket, data, size, context, 0);
provider->RecvAsync(socket, buffer, size, context, 0);

// Process completions
CompletionEntry entries[64];
int count = provider->ProcessCompletions(entries, 64, 100);
```

### Example 3: Switching I/O Backend / 예제 3: I/O 백엔드 전환

```cpp
// Create specific provider
auto provider = AsyncIO::CreateAsyncIOProvider("RIO"); // Try RIO first

if (!provider || provider->Initialize(1024, 128) != AsyncIOError::Success) {
    // Fallback to standard IOCP
    provider = AsyncIO::CreateAsyncIOProvider("IOCP");
    provider->Initialize(1024, 128);
}
```

---

## Performance Characteristics / 성능 특성

### IOCPNetworkEngine
- ✅ Zero-copy Session management (direct IOContext ownership)
- ✅ Optimized for server workloads
- ✅ Integrated thread pool and event system
- ❌ Windows-only

### AsyncIOProvider
- ✅ Cross-platform compatibility
- ✅ Runtime backend selection
- ✅ Batch processing support (RIO, io_uring)
- ✅ Buffer pre-registration (RIO, io_uring)
- ⚠️ Small overhead for abstraction layer

---

## Conclusion / 결론

**Both layers coexist for good reasons:**
**두 계층이 공존하는 이유:**

1. **IOCPNetworkEngine**: Production-ready server solution
   - 제품화 가능한 서버 솔루션

2. **AsyncIOProvider**: Reusable cross-platform I/O library
   - 재사용 가능한 크로스 플랫폼 I/O 라이브러리

**Choose based on your needs:**
**필요에 따라 선택:**

- Server app → IOCPNetworkEngine
- Library → AsyncIOProvider
- 서버 앱 → IOCPNetworkEngine
- 라이브러리 → AsyncIOProvider
