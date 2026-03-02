# Network Module Architecture / 네트워크 모듈 아키텍처

## Overview / 개요

This document describes the current architecture of the network module and how to use it.
이 문서는 네트워크 모듈의 현재 아키텍처와 사용 방법을 설명합니다.

---

## Two-Layer Architecture / 2계층 아키텍처

### Layer 1: High-Level Network Engine
### 계층 1: 고수준 NetworkEngine

**Core Types / 핵심 타입:**
- `INetworkEngine` (interface)
- `BaseNetworkEngine` (common logic)
- `WindowsNetworkEngine`, `LinuxNetworkEngine`, `macOSNetworkEngine`

**Purpose / 목적:**
- Complete server solution with Session management
- Session 관리를 포함한 완전한 서버 솔루션

**Features / 기능:**
- Listen socket management
- Accept thread for incoming connections
- Session lifecycle management (Connect/Disconnect)
- Event-driven callbacks (Connected, Disconnected, DataReceived)
- **KeyedDispatcher** for business logic (key-affinity, session ordering without mRecvMutex) — 2026-03-02
- **TimerQueue** for session timeout checks and periodic tasks — 2026-03-02
- **NetworkEventBus** for multi-subscriber event broadcasting — 2026-03-02
- Statistics tracking

**When to Use / 사용 시기:**
- Building server applications (TestServer, TestDBServer)
- Need Session-based connection management
- Want integrated event system

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
- Need to switch I/O backends dynamically
- Session-independent I/O operations
- Advanced performance optimization scenarios

---

## Design Rationale / 설계 근거

**Reason 1: Different Abstraction Levels / 이유 1: 다른 추상화 수준**
- NetworkEngine: Application-level (Session, Events, Callbacks)
- AsyncIOProvider: System-level (Raw I/O operations)

**Reason 2: Different Use Cases / 이유 2: 다른 사용 사례**
- NetworkEngine: Server applications (TestServer, TestDBServer)
- AsyncIOProvider: Cross-platform libraries, tools, benchmarks

---

## Code Organization / 코드 구성

```
Server/ServerEngine/
├── Core/
│   └── Memory/                          # 플랫폼 독립 버퍼 풀 모듈 (2026-03-01 신규)
│       ├── IBufferPool.h                # BufferSlot + IBufferPool 인터페이스
│       ├── StandardBufferPool.h/.cpp    # _aligned_malloc/posix_memalign, Group A
│       ├── RIOBufferPool.h/.cpp         # VirtualAlloc+RIORegisterBuffer, Group B
│       └── IOUringBufferPool.h/.cpp     # posix_memalign+io_uring_register_buffers, Group C
├── Network/
│   ├── Core/
│   │   ├── NetworkEngine.h              # INetworkEngine
│   │   ├── BaseNetworkEngine.h/.cpp     # common logic (KeyedDispatcher+TimerQueue 내장)
│   │   ├── NetworkEngineFactory.cpp     # CreateNetworkEngine
│   │   ├── AsyncIOProvider.h            # low-level I/O interface
│   │   ├── Session.h/.cpp               # Send()→SendResult, AsyncScope 통합 (2026-03-02)
│   │   ├── SessionManager.h/.cpp
│   │   ├── SendBufferPool.h/.cpp        # IOCP 전송 풀 (2026-03-01 추가)
│   │   ├── NetworkEventBus.h/.cpp       # 다중 구독자 이벤트 버스 (2026-03-02 신규)
│   │   └── PacketDefine.h
│   └── Platforms/
│       ├── WindowsNetworkEngine.h/.cpp
│       ├── LinuxNetworkEngine.h/.cpp
│       └── macOSNetworkEngine.h/.cpp
└── Platforms/
    ├── Windows/
    │   ├── IocpAsyncIOProvider.*
    │   └── RIOAsyncIOProvider.*         # Core::Memory::RIOBufferPool 사용 (2026-03-01 리팩토링)
    ├── Linux/
    │   ├── EpollAsyncIOProvider.*
    │   └── IOUringAsyncIOProvider.*
    └── macOS/
        └── KqueueAsyncIOProvider.*
```

---

## Usage Examples / 사용 예제

### Example 1: Building a Server / 서버 구축

```cpp
#include "Network/Core/NetworkEngine.h"

using namespace Network::Core;

int main() {
    auto engine = CreateNetworkEngine("auto");
    if (!engine) return -1;

    engine->RegisterEventCallback(
        NetworkEvent::Connected,
        [](const NetworkEventData& data) {
            std::cout << "Client connected: " << data.connectionId << std::endl;
        });

    engine->RegisterEventCallback(
        NetworkEvent::DataReceived,
        [&engine](const NetworkEventData& data) {
            engine->SendData(data.connectionId, data.data.get(), data.dataSize);
        });

    if (!engine->Initialize(1000, 8080)) return -1;
    if (!engine->Start()) return -1;

    std::cout << "Server running..." << std::endl;
    std::cin.get();

    engine->Stop();
    return 0;
}
```

### Example 2: AsyncIOProvider / 저수준 I/O

```cpp
auto provider = AsyncIO::CreateAsyncIOProvider();
provider->Initialize(1024, 128);
provider->SendAsync(socket, buffer, size, context, 0);
provider->RecvAsync(socket, buffer, size, context, 0);
```

---

## SendBufferPool / IOCP 전송 버퍼 풀

**파일**: `Network/Core/SendBufferPool.h/.cpp`
**추가일**: 2026-03-01

IOCP 전송 경로의 per-send 힙 할당을 제거하기 위한 싱글턴 풀.

```
┌─────────────────────────────────────┐
│ SendBufferPool (singleton)          │
│  poolSize × slotSize 연속 메모리    │
│  O(1) Acquire / O(1) Release        │
├─────────────────────────────────────┤
│ Session::Send()  →  Acquire slot    │
│   memcpy data → slot (1회 복사)     │
│ Session::PostSend() →              │
│   wsaBuf.buf = slot ptr (zero-copy) │
│ IOCP 완료 →  Release slot          │
└─────────────────────────────────────┘
```

- **RIO 경로**: `RIOAsyncIOProvider`가 `Core::Memory::RIOBufferPool mRecvPool, mSendPool`을 멤버로 소유 (별개)
- **초기화**: `WindowsNetworkEngine::InitializePlatform()` (IOCP 모드만)
- **슬롯 수**: `maxConnections × 4` (소켓당 동시 4 전송 기준)

---

## Current Status / 현재 상태

- **Windows**: primary development/test path (RIO/IOCP 모두 1000 클라이언트 PASS)
- **Linux/macOS**: 기본 send/recv 경로 구현 완료, 테스트/검증 필요

### 최근 성능 최적화 이력 (Windows)

| 날짜 | 항목 | 효과 |
|------|------|------|
| 2026-02-16 | ProcessRawRecv O(1) 오프셋 기반 TCP 재조립 | 패킷당 O(n) 제거 |
| 2026-02-28 | RIOAsyncIOProvider slab pool (WSA 10055 수정) | 1000 동접 PASS |
| 2026-03-01 | AsyncBufferPool O(1) 프리리스트 | 슬롯 탐색 O(n)→O(1) |
| 2026-03-01 | ProcessRawRecv 배치 버퍼 + 패스트패스 | alloc N→1/0 |
| 2026-03-01 | SendBufferPool zero-copy IOCP 전송 | per-send alloc 제거 |
| 2026-03-01 | Core/Memory 버퍼 모듈 (RIOBufferPool 분리) | RIOAsyncIOProvider 내부 slab → 독립 풀 클래스 |
| 2026-03-01 | PingPong 검증 페이로드 추가 | 매 왕복 버퍼 무결성 자동 검증 |
| 2026-03-02 | KeyedDispatcher 교체 (mRecvMutex 제거) | 세션별 직렬화 락 제거, recv 경합 0 |
| 2026-03-02 | TimerQueue 신규 / DB 핑 스레드 교체 | sleep 기반 스레드 제거, 단일 타이머 스레드 |
| 2026-03-02 | AsyncScope 통합 / Send 백프레셔 | Close 후 큐 작업 억제, 큐 포화 명시적 반환 |
| 2026-03-02 | NetworkEventBus 신규 | 이벤트당 단일→다중 구독자 지원 |

---

## Core/Memory 버퍼 풀 모듈

**디렉토리**: `Core/Memory/`
**추가일**: 2026-03-01

플랫폼별 버퍼/메모리 풀 전략을 네트워크 모듈에 종속되지 않는 독립 모듈로 분리.

| 클래스 | 플랫폼 | 할당 방식 | 특징 |
|--------|--------|-----------|------|
| `StandardBufferPool` | All | `_aligned_malloc` / `posix_memalign` | IOCP·epoll·kqueue 공용 |
| `RIOBufferPool` | Windows | `VirtualAlloc` + 1× `RIORegisterBuffer` | `GetSlabId()` / `GetRIOOffset()` / `SlotPtr()` |
| `IOUringBufferPool` | Linux | `posix_memalign` + `io_uring_register_buffers` | `InitializeFixed(ring, …)` / `GetFixedBufferIndex()` |

```
BufferSlot Acquire()  →  { ptr, index, capacity }
void       Release(index)
```

**락 순서 (RIOBufferPool + RIOAsyncIOProvider)**:
`mMutex`(Provider) → pool 내부 mutex (단방향, deadlock 없음)

---

## NetworkEventBus (2026-03-02 신규)

**파일**: `Network/Core/NetworkEventBus.h/.cpp`

`BaseNetworkEngine::FireEvent()` 는 기존 단일 콜백(`RegisterEventCallback`) 호출 후 `NetworkEventBus::Instance().Publish()` 도 호출한다. 다중 구독자가 같은 이벤트를 독립 채널로 수신 가능.

```cpp
auto ch = std::make_shared<Concurrency::Channel<Core::NetworkBusEventData>>(/* opts */);
auto handle = Core::NetworkEventBus::Instance().Subscribe(
    Core::NetworkEvent::Connected, ch);

// 별도 스레드에서 소비
while (auto evt = ch->Receive(100)) {
    Logger::Info("New conn: " + std::to_string(evt->connectionId));
}
Core::NetworkEventBus::Instance().Unsubscribe(handle);
```

**`NetworkBusEventData`** 는 `vector<uint8_t> data` (copyable) 를 사용 — `NetworkEventData` 의 `unique_ptr<uint8_t[]>` 와 다름.

## Conclusion / 결론

- **NetworkEngine**: production-facing server abstraction (KeyedDispatcher + TimerQueue + NetworkEventBus 내장)
- **AsyncIOProvider**: low-level, reusable async I/O building block
- **Core/Memory**: platform-agnostic buffer pool module (`IBufferPool` + 3 concrete implementations)
- **SendBufferPool**: IOCP 전용 zero-copy 전송 버퍼 관리자
- **NetworkEventBus**: 다중 구독자 이벤트 버스 싱글턴 (2026-03-02 신규)

Choose the layer that fits your use case.

