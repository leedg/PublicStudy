# 아키텍처

## 1. 목표
비동기 네트워크 엔진과 테스트 서버/클라이언트를 통해 핵심 네트워크/패킷/DB 흐름을 검증합니다.

## 2. 시스템 구성
TestClient -> TestServer -> TestDBServer (옵션)
      |           |
      +-----------+ ServerEngine

기본 포트
- TestServer: 9000
- TestDBServer: 8001 (run_test.ps1 기본값은 8002)

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
- INetworkEngine + BaseNetworkEngine: 공통 로직 (이벤트, 통계, 세션 관리)
- Platform NetworkEngine: Windows(IOCP/RIO), Linux(epoll/io_uring), macOS(kqueue)
- AsyncIOProvider: 플랫폼별 저수준 백엔드
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
- TestServer <-> TestDBServer는 `ServerPacketDefine` 기반(Ping/Pong, DBSavePingTime)
- MessageHandler 포맷은 `DBServer.cpp` 실험 경로에서만 사용(기본 실행 경로 아님)
- TestServer의 DBTaskQueue/DB 풀은 `ENABLE_DATABASE_SUPPORT` 정의 시 활성 (현재는 로그/플레이스홀더)
- TestDBServer는 Ping/DBSavePingTime 패킷 처리 가능 (DB 저장은 플레이스홀더)

## 7. 제약 및 향후 과제
- Linux/macOS 경로는 기본 send/recv 구현 완료 (테스트/안정성 검증 필요)
- TestServer ↔ TestDBServer 패킷 처리 연결 강화 필요
- DB CRUD 실연동 필요
- TLS/인증/압축 미구현
