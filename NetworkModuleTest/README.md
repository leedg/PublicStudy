# NetworkModuleTest

분산 서버 아키텍처용 비동기 네트워크 모듈과 테스트 서버/클라이언트 프로젝트입니다.

## 개요
- 목표: ServerEngine 기반 네트워크/DB 모듈을 TestServer/TestDBServer/TestClient로 검증
- 핵심: AsyncIOProvider, IOCPNetworkEngine, Session/Packet, Database 모듈

### 런타임 흐름
TestClient -> TestServer -> TestDBServer (옵션)
      |           |
      +-----------+ ServerEngine (AsyncIO, Utils, Database, Protocols)

### 프로젝트 구조
```text
NetworkModuleTest/
  Doc/
  Server/
    ServerEngine/
    TestServer/
    DBServer/
  Client/
    TestClient/
  ModuleTest/
    DBModuleTest/
    MultiPlatformNetwork/
  run_test.ps1
  run_test.bat
```

## 상태 (2026-02-04)

| 모듈 | 상태 | 비고 |
| --- | --- | --- |
| ServerEngine | 진행 중 | IOCP 엔진, AsyncIOProvider, Database 모듈 구현. Linux/macOS provider 소스 포함 |
| TestServer | 프로토타입 | IOCP 기반 접속/세션/핑 처리. DB 풀은 `ENABLE_DATABASE_SUPPORT` 정의 시 활성 |
| TestDBServer | 프로토타입 | AsyncIOProvider + MessageHandler + Ping/Pong. 네트워크 accept/send 미구현, DB CRUD 스텁 |
| TestClient | 프로토타입 | SessionConnect + Ping/Pong + RTT 통계 |
| DBModuleTest | 레거시/테스트 | 독립 DB 모듈 테스트. 신규 코드는 ServerEngine/Database 권장 |
| MultiPlatformNetwork | 완료/보관 | 크로스 플랫폼 AsyncIO 참고 구현 |

## 기술 스택
- C++17
- Visual Studio 2022 (주 빌드)
- CMake (부분적, MultiPlatformNetwork 위주)
- Async I/O: IOCP/RIO, epoll/io_uring, kqueue
- Database: ODBC/OLEDB (모듈 구현)
- Serialization: Protobuf (옵션, Ping/Pong 핸들러)

## 빠른 시작 (Windows)
1. `NetworkModuleTest.sln` 열고 x64 Debug/Release 빌드
2. `TestDBServer.exe -p 8002` 실행
3. `TestServer.exe -p 9000` 실행 (DB 연결 필요 시 `-d "<connstr>"`)
4. `TestClient.exe --host 127.0.0.1 --port 9000` 실행
5. 자동 실행: `run_test.ps1` 또는 `run_test.bat`

> 참고: TestServer의 DB 연결 옵션은 `ENABLE_DATABASE_SUPPORT`가 정의된 빌드에서만 동작합니다.

## 문서
- `Doc/ProjectOverview.md`
- `Doc/Architecture.md`
- `Doc/Protocol.md`
- `Doc/API.md`
- `Doc/Development.md`
- `Doc/DevelopmentGuide.md`
- `Doc/SolutionGuide.md`
- `Server/ServerEngine/Database/README.md`
- `ModuleTest/DBModuleTest/README.md`

## 다음 단계
1. TestDBServer의 실제 accept/send 로직 구현
2. TestServer의 패킷 처리 확장 및 DBServer 연동 강화
3. Protobuf 경로 및 테스트 타깃 정리
4. CMake 스크립트 정합성 개선
