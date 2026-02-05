# 아키텍처

## 1. 목표
비동기 네트워크 엔진과 테스트 서버/클라이언트를 통해 핵심 네트워크/패킷/DB 흐름을 검증합니다.

## 2. 시스템 구성
TestClient -> TestServer -> TestDBServer (옵션)
      |           |
      +-----------+ ServerEngine

기본 포트
- TestServer: 9000
- TestDBServer: 8002

## 3. 디렉터리 구조
```text
NetworkModuleTest/
  Doc/
  Server/
    ServerEngine/
      Network/Core/
      Platforms/Windows|Linux|macOS/
      Database/
      Implementations/Protocols/
      Tests/Protocols/
      Utils/
    TestServer/
    DBServer/
  Client/TestClient/
  ModuleTest/
    DBModuleTest/
    MultiPlatformNetwork/
```

## 4. ServerEngine 구성
- AsyncIOProvider: 플랫폼별 백엔드(IOCP/RIO, epoll/io_uring, kqueue)
- IOCPNetworkEngine: Windows IOCP 서버 구현체
- Session/SessionManager: 연결 및 세션 관리
- PacketDefine: SessionConnect/Ping/Pong 바이너리 프레이밍
- Database: ConnectionPool, ODBC/OLEDB 구현
- Utils: Logger, Timer, ThreadPool 등

## 5. Client <-> Server 플로우
1. Client가 TCP 연결 후 SessionConnectReq 전송
2. Server가 SessionConnectRes로 sessionId 전달
3. Client가 주기적으로 PingReq 전송
4. Server가 PongRes로 응답

## 6. DBServer 연동
- TestServer <-> TestDBServer는 MessageHandler 포맷 기반
- TestServer의 DB 풀은 `ENABLE_DATABASE_SUPPORT` 정의 시 활성
- DB CRUD 메시지는 계획 단계이며 현재는 스텁

## 7. 제약 및 향후 과제
- TestDBServer의 실제 네트워크 accept/send 로직 필요
- 패킷 핸들러 확장
- TLS/인증/압축 미구현
