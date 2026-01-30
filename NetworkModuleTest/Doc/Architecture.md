# 분산 서버 아키텍처 개발 명세

## 1. 프로젝트 개요

### 1.1 목표
NetworkModuleTest 프로젝트는 고성능 비동기 네트워크 모듈을 기반으로 한 분산 서버 아키텍처를 구현하는 것을 목표로 합니다.

### 1.2 아키텍처 개요
```
Client → TestServer → DBServer
         ↓              ↓
    로직 처리      데이터베이스 처리
```

- **TestServer**: 클라이언트 요청 처리 및 비즈니스 로직 수행
- **DBServer**: 데이터베이스 전용 처리 서버
- **ServerEngine**: 모든 서버의 기반이 되는 네트워크 모듈

## 2. 시스템 구조

### 2.1 디렉토리 구조
```
NetworkModuleTest/
├── Doc/                           # 개발 명세 및 문서
│   ├── Architecture.md            # 아키텍처 명세
│   ├── API.md                    # API 명세
│   ├── Protocol.md               # 프로토콜 명세
│   └── Development.md            # 개발 가이드
├── Server/                        # 서버 구현
│   ├── DBServer/                  # 데이터베이스 서버
│   ├── TestServer/                # 로직 처리 서버 (Application)
│   │   ├── include/TestServer.h   # GameSession + TestServer 클래스
│   │   ├── src/TestServer.cpp     # 서버 구현체
│   │   └── main.cpp              # 진입점 (CLI 인자, 시그널 핸들러)
│   └── ServerEngine/             # 네트워크 엔진 (Static Library)
│       ├── Network/Core/          # 핵심 모듈
│       │   ├── AsyncIOProvider.h  # 비동기 IO 추상화
│       │   ├── NetworkEngine.h    # INetworkEngine 인터페이스
│       │   ├── IOCPNetworkEngine.h/cpp  # IOCP 기반 엔진 구현
│       │   ├── Session.h/cpp      # 세션 관리 (연결 단위)
│       │   ├── SessionManager.h/cpp    # 세션 팩토리 + 전체 관리
│       │   ├── PacketDefine.h     # 바이너리 패킷 헤더/구조체
│       │   └── PlatformDetect.h   # 플랫폼 감지
│       ├── Platforms/             # 플랫폼별 AsyncIO 구현
│       │   ├── Windows/           # IOCP, RIO
│       │   ├── Linux/             # epoll, io_uring
│       │   └── macOS/             # kqueue
│       ├── Database/              # 데이터베이스 모듈
│       │   ├── DBConnection.h/cpp      # ODBC 기반 DB 연결
│       │   └── DBConnectionPool.h/cpp  # 커넥션 풀 (RAII)
│       ├── Tests/Protocols/       # 프로토콜 모듈
│       │   ├── MessageHandler.h/cpp    # 메시지 핸들러
│       │   └── PingPong.h/cpp     # Protobuf PingPong
│       └── Utils/                 # 유틸리티
│           └── NetworkUtils.h     # Logger, ThreadPool, SafeQueue, Timer 등
└── Client/                        # 클라이언트
    └── TestClient/                # 테스트 클라이언트
```

### 2.2 서버 역할 분담

#### DBServer (데이터베이스 서버)
- **주요 역할**: 데이터베이스 CRUD 작업 전담
- **처리 기능**:
  - 데이터 조회 (SELECT)
  - 데이터 삽입 (INSERT)
  - 데이터 수정 (UPDATE)
  - 데이터 삭제 (DELETE)
  - 트랜잭션 관리
  - 커넥션 풀 관리
- **특징**:
  - 데이터베이스 커넥션 최적화
  - 쿼리 성능 모니터링
  - 데이터 일관성 보장

#### TestServer (로직 처리 서버)
- **주요 역할**: 클라이언트 요청 처리 및 비즈니스 로직 수행
- **처리 기능**:
  - 클라이언트 연결 관리
  - 요청 인증 및 권한 확인
  - 비즈니스 로직 처리
  - DBServer로 데이터 요청 전송
  - 응답 데이터 조합 및 클라이언트 전송
- **특징**:
  - 상태less 설계
  - 수평 확장 용이
  - 부하 분산 지원

## 3. 네트워크 엔진 설계

### 3.1 프로젝트 빌드 구조
- **ServerEngine**: Static Library (.lib) — 재사용 가능한 엔진 코드
- **TestServer**: Application (.exe) — 게임 로직, ServerEngine.lib 링크
- **의존 라이브러리**: `WS2_32.lib`, `odbc32.lib`

### 3.2 비동기 처리 아키텍처 (Network/Logic 분리)
```
┌─────────────────────────────────────────────────────────┐
│                    IOCPNetworkEngine                     │
├──────────────────┬──────────────────────────────────────┤
│  AcceptThread    │  WorkerThread (N개)                   │
│  ┌────────────┐  │  ┌──────────────────────────────┐    │
│  │ accept()   │  │  │ GetQueuedCompletionStatus()  │    │
│  │ → Session  │  │  │ → Recv 완료 감지              │    │
│  │   생성     │  │  │ → 데이터 복사                 │    │
│  │ → IOCP     │  │  │ → LogicThreadPool에 디스패치  │    │
│  │   등록     │  │  │ → PostRecv() 재등록           │    │
│  │ → PostRecv │  │  └──────────────────────────────┘    │
│  └────────────┘  │                                      │
├──────────────────┴──────────────────────────────────────┤
│                  LogicThreadPool                         │
│  ┌──────────────────────────────────────────────┐       │
│  │ Session::OnRecv() → 패킷 파싱 → 핸들러 호출  │       │
│  │ Session::OnConnected() → DB 로깅              │       │
│  │ Session::OnDisconnected() → 정리              │       │
│  └──────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────┘
```

**핵심 원칙**: IOCP WorkerThread는 IO 완료만 처리하고, 비즈니스 로직은 별도 ThreadPool에서 비동기 실행

### 3.3 세션 생명주기
```
None → Connecting → Connected → Disconnecting → Disconnected
         │              │              │
         │ accept()     │ OnRecv()     │ Close()
         │ PostRecv()   │ Send()       │ OnDisconnected()
         │ OnConnected()│              │ SessionManager 제거
```

### 3.4 패킷 구조 (Binary Framing)
```
┌──────────────────────────┐
│ PacketHeader (4 bytes)   │
│ ┌──────────┬───────────┐ │
│ │ size(2B) │ id(2B)    │ │
│ └──────────┴───────────┘ │
├──────────────────────────┤
│ Payload (가변)           │
└──────────────────────────┘
```

**패킷 타입**:
| ID     | 이름               | 방향            | 설명              |
|--------|--------------------|-----------------|--------------------|
| 0x0001 | SessionConnectReq  | Client → Server | 접속 요청          |
| 0x0002 | SessionConnectRes  | Server → Client | 접속 응답          |
| 0x0003 | PingReq            | Client → Server | Ping 요청          |
| 0x0004 | PongRes            | Server → Client | Pong 응답          |

### 3.5 서버간 통신 프로토콜
- **바이너리 패킷**: `PacketDefine.h` 기반 (`#pragma pack(push, 1)`)
- **Protobuf**: 기존 `PingPong.h/cpp` 유지 (MessageHandler 통한 처리)
- **DB 연동**: ODBC 기반 커넥션 풀 (ScopedDBConnection RAII)

### 3.6 핵심 프로토콜 흐름

#### 클라이언트 접속 흐름
```
Client                  TestServer (IOCP)              DB
  |                         |                           |
  |--- TCP Connect -------->|                           |
  |                    accept() → GameSession 생성      |
  |                    IOCP 등록 + PostRecv()            |
  |                    OnConnected() ─────────────────> INSERT SessionLog
  |                         |                           |
  |--- SessionConnectReq -->|                           |
  |                    HandleConnectRequest()            |
  |<-- SessionConnectRes ---|                           |
  |                         |                           |
```

#### Ping/Pong 흐름
```
Client                  TestServer
  |                         |
  |--- PingReq ----------->|
  |    (clientTime, seq)    |
  |                    HandlePingRequest()
  |<-- PongRes ------------|
  |    (clientTime,         |
  |     serverTime, seq)    |
```

## 4. 성능 목표

### 4.1 처리량
- **TestServer**: 10,000+ RPS (Requests Per Second)
- **DBServer**: 5,000+ QPS (Queries Per Second)
- **네트워크 지연**: < 1ms (서버간)

### 4.2 동시성
- **최대 동시 접속**: 10,000+ 클라이언트
- **동시 처리 요청**: 1,000+ 개
- **메모리 사용**: < 2GB (서버당)

## 5. 확장성 설계

### 5.1 수평 확장
- **TestServer**: 다중 인스턴스 지원 (Load Balancer)
- **DBServer**: 읽기 전용 복제본 지원
- **Auto Scaling**: 부하에 따른 자원 동적 할당

### 5.2 모듈화
- **독립적 배포**: 각 서버 독립 배포 가능
- **플러그인 아키텍처**: 기능 모듈 확장 용이
- **API 버전관리**: 하위 호환성 보장

## 6. 개발 단계

### Phase 1: 기반 구조
- [x] ServerEngine Static Library 전환
- [x] INetworkEngine 인터페이스 정의
- [x] IOCPNetworkEngine 구현 (Accept/Worker 스레드)
- [x] Session / SessionManager 구현
- [x] PacketDefine.h 바이너리 패킷 정의
- [x] DBConnection / DBConnectionPool (ODBC) 구현
- [x] TestServer GameSession 통합 (접속/Ping/DB로깅)
- [ ] Ping/Pong 프로토콜 E2E 테스트

### Phase 2: 핵심 기능
- [ ] TestClient 구현 (접속 + PingPong 테스트)
- [ ] DBServer 데이터베이스 전담 서버
- [ ] 서버간 CRUD 프로토콜 구현
- [ ] epoll/kqueue 기반 NetworkEngine 추가

### Phase 3: 고급 기능
- [ ] 인증/권한 시스템
- [ ] 성능 최적화 (RIO, io_uring)
- [ ] 모니터링 및 로깅 강화

### Phase 4: 확장 및 안정화
- [ ] 부하 테스트
- [ ] 장애 조치 기능
- [ ] 배포 자동화

## 7. 기술 스택

### 7.1 핵심 기술
- **언어**: C++17
- **빌드**: CMake 3.15+
- **네트워크**: AsyncIO (IOCP/epoll/kqueue)
- **직렬화**: Protobuf 3.x
- **데이터베이스**: TBD (MySQL/PostgreSQL/SQLite)

### 7.2 개발 도구
- **버전관리**: Git
- **테스트**: GTest
- **문서**: Markdown
- **CI/CD**: TBD (GitHub Actions/Jenkins)

## 8. 품질 목표

### 8.1 안정성
- **가동시간**: 99.9%+
- **장애 복구**: < 30초
- **데이터 손실**: 0%

### 8.2 보안
- **통신 암호화**: TLS 1.3+
- **인증**: JWT/OAuth 2.0
- **취약점**: OWASP Top 10 대응

### 8.3 유지보수성
- **코드 커버리지**: 80%+
- **문서화**: 모든 API 명세화
- **테스트**: 단위/통합/부하 테스트

---

*본 문서는 NetworkModuleTest 분산 서버 아키텍처 개발을 위한 최초 명세이며, 개발 진행에 따라 지속적으로 업데이트됩니다.*