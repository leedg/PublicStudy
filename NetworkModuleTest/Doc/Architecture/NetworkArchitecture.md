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
- Thread pool for business logic
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
├── Network/
│   ├── Core/
│   │   ├── NetworkEngine.h              # INetworkEngine
│   │   ├── BaseNetworkEngine.h/.cpp     # common logic
│   │   ├── NetworkEngineFactory.cpp     # CreateNetworkEngine
│   │   ├── AsyncIOProvider.h            # low-level I/O interface
│   │   ├── Session.h/.cpp
│   │   └── SessionManager.h/.cpp
│   └── Platforms/
│       ├── WindowsNetworkEngine.h/.cpp
│       ├── LinuxNetworkEngine.h/.cpp
│       └── macOSNetworkEngine.h/.cpp
└── Platforms/
    ├── Windows/
    │   ├── IocpAsyncIOProvider.*
    │   └── RIOAsyncIOProvider.*
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

## Current Status / 현재 상태

- **Windows**: primary development/test path
- **Linux/macOS**: 기본 send/recv 경로 구현 완료, 테스트/검증 필요

---

## Conclusion / 결론

- **NetworkEngine**: production-facing server abstraction
- **AsyncIOProvider**: low-level, reusable async I/O building block

Choose the layer that fits your use case.

