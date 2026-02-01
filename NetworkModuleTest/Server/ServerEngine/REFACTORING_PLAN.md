# ServerEngine Refactoring Plan

## 목표

1. 기능별로 폴더 구조 분리
2. Interface와 구현부 분리
3. MessageHandler, DB 처리 등을 각 서버(TestServer)에서 구현하도록 변경
4. Visual Studio 필터 정리

## 현재 구조

```
ServerEngine/
├── Network/
│   └── Core/
│       ├── AsyncIOProvider.h/cpp
│       ├── IOCPNetworkEngine.h/cpp
│       ├── NetworkEngine.h
│       ├── PacketDefine.h
│       ├── PlatformDetect.h/cpp
│       ├── Session.h/cpp
│       └── SessionManager.h/cpp
├── Platforms/
│   ├── Windows/
│   │   ├── IocpAsyncIOProvider.h/cpp
│   │   └── RIOAsyncIOProvider.h/cpp
│   ├── Linux/
│   │   ├── EpollAsyncIOProvider.h/cpp
│   │   └── IOUringAsyncIOProvider.h/cpp
│   └── macOS/
│       └── KqueueAsyncIOProvider.h/cpp
├── Database/
│   ├── IDatabase.h (Interface)
│   ├── DatabaseFactory.h/cpp
│   ├── ODBCDatabase.h/cpp (Implementation)
│   ├── OLEDBDatabase.h/cpp (Implementation)
│   ├── ConnectionPool.h/cpp
│   ├── DBConnection.h/cpp (Legacy)
│   └── DBConnectionPool.h/cpp (Legacy)
├── Tests/
│   └── Protocols/
│       ├── MessageHandler.h/cpp (Should be Interface)
│       └── PingPong.h/cpp (Test Implementation)
└── Utils/
    └── NetworkUtils.h
```

## 새로운 구조 (리팩토링 후)

```
ServerEngine/
├── Core/                           # 핵심 엔진 기능
│   ├── Network/
│   │   ├── INetworkEngine.h        # Network Engine Interface
│   │   ├── IOCPNetworkEngine.h/cpp # IOCP Implementation
│   │   ├── Session.h/cpp           # Session Management
│   │   ├── SessionManager.h/cpp    # Session Manager
│   │   └── PacketDefine.h          # Packet Definitions
│   ├── Platform/
│   │   ├── IPlatformProvider.h     # Platform Interface
│   │   ├── AsyncIOProvider.h/cpp   # Base Provider
│   │   └── PlatformDetect.h/cpp    # Platform Detection
│   └── Threading/                  # (Future: Thread Pool, etc.)
│
├── Interfaces/                     # 추상 인터페이스 계층
│   ├── IDatabase.h                 # Database Interface
│   ├── IMessageHandler.h           # Message Handler Interface
│   ├── IProtocolHandler.h          # Protocol Handler Interface
│   └── ILogger.h                   # Logger Interface (Future)
│
├── Implementations/                # 기본 구현체
│   ├── Database/
│   │   ├── DatabaseFactory.h/cpp   # Factory
│   │   ├── ODBCDatabase.h/cpp      # ODBC Implementation
│   │   ├── OLEDBDatabase.h/cpp     # OLEDB Implementation
│   │   ├── ConnectionPool.h/cpp    # Connection Pool
│   │   └── Legacy/                 # Deprecated
│   │       ├── DBConnection.h/cpp
│   │       └── DBConnectionPool.h/cpp
│   └── Protocols/
│       ├── BaseMessageHandler.h/cpp # Base Message Handler
│       └── Examples/               # Example implementations
│           └── PingPong.h/cpp
│
├── Platform/                       # 플랫폼별 구현
│   ├── Windows/
│   │   ├── IocpAsyncIOProvider.h/cpp
│   │   └── RIOAsyncIOProvider.h/cpp
│   ├── Linux/
│   │   ├── EpollAsyncIOProvider.h/cpp
│   │   └── IOUringAsyncIOProvider.h/cpp
│   └── macOS/
│       └── KqueueAsyncIOProvider.h/cpp
│
├── Utils/                          # 유틸리티
│   ├── NetworkUtils.h
│   ├── TimeUtils.h                 # (Future)
│   └── StringUtils.h               # (Future)
│
└── ServerEngine.h                  # Main include header
```

## TestServer 구조 (구현부)

```
TestServer/
├── include/
│   ├── TestServer.h
│   ├── TestMessageHandler.h        # MessageHandler 구현
│   ├── TestDatabaseManager.h       # Database 관리 구현
│   └── TestProtocolHandler.h       # Protocol 처리 구현
├── src/
│   ├── TestServer.cpp
│   ├── TestMessageHandler.cpp      # 실제 메시지 처리 로직
│   ├── TestDatabaseManager.cpp     # 실제 DB 작업 로직
│   └── TestProtocolHandler.cpp     # 실제 프로토콜 로직
└── main.cpp
```

## 마이그레이션 단계

### Phase 1: Interface 분리 ✓
1. IMessageHandler.h 생성
2. IProtocolHandler.h 생성
3. BaseMessageHandler 생성 (기본 구현)

### Phase 2: 폴더 구조 변경 ✓
1. Core/ 폴더 생성 및 이동
2. Interfaces/ 폴더 생성
3. Implementations/ 폴더 생성
4. 기존 파일 이동

### Phase 3: TestServer 구현 ✓
1. TestMessageHandler 구현
2. TestDatabaseManager 구현
3. main.cpp에서 의존성 주입

### Phase 4: Visual Studio 필터 정리 ✓
1. 새 폴더 구조에 맞게 필터 재구성
2. 논리적 그룹핑 적용

### Phase 5: 문서화 및 정리 ✓
1. README 업데이트
2. 마이그레이션 가이드 작성
3. 예제 코드 업데이트

## 주요 변경 사항

### 1. IMessageHandler.h (새 인터페이스)
```cpp
namespace Network::Interfaces {
    class IMessageHandler {
    public:
        virtual ~IMessageHandler() = default;

        virtual bool ProcessMessage(
            ConnectionId connectionId,
            const uint8_t* data,
            size_t size) = 0;

        virtual std::vector<uint8_t> CreateMessage(
            MessageType type,
            ConnectionId connectionId,
            const void* data,
            size_t size) = 0;
    };
}
```

### 2. BaseMessageHandler (기본 구현)
- 공통 기능 제공
- 타임스탬프, 유효성 검증 등
- 각 서버에서 상속하여 사용

### 3. TestMessageHandler (TestServer 구현)
```cpp
namespace TestServer {
    class TestMessageHandler : public Network::Implementations::BaseMessageHandler {
    public:
        bool ProcessMessage(...) override {
            // TestServer specific logic
        }
    };
}
```

## 이점

1. **명확한 책임 분리**
   - ServerEngine: 인프라 제공
   - TestServer: 비즈니스 로직 구현

2. **재사용성 향상**
   - Interface 기반 설계
   - 다른 서버 프로젝트에서도 동일한 엔진 사용 가능

3. **유지보수성 개선**
   - 기능별로 명확히 분리된 폴더 구조
   - 의존성 관계 명확화

4. **테스트 용이성**
   - Interface를 Mock으로 대체 가능
   - 단위 테스트 작성 용이

5. **확장성**
   - 새로운 프로토콜, DB 추가 용이
   - 플랫폼 추가 용이
