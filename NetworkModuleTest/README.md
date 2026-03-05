# NetworkModuleTest

C++17 기반의 네트워크 테스트베드입니다. 현재 기본 런타임 흐름은 `TestClient -> TestServer -> TestDBServer(옵션)` 이며, ServerEngine의 플랫폼별 네트워크 엔진(Windows/Linux/macOS)을 검증하는 목적에 맞춰 구성되어 있습니다.

## 현재 상태 (2026-03-05 기준)

- 기본 클라이언트-서버 프로토콜: `SessionConnectReq/Res`, `PingReq/PongRes` (고정 바이너리 구조체)
- TestServer 로컬 DB 경로: `DBTaskQueue + Mock/SQLite`
- TestServer <-> TestDBServer 경로: `ServerPacketDefine` 기반 (Ping/Pong, DBSavePingTime 중심)
- Linux 통합 테스트: `test_linux/` 경로에서 epoll/io_uring 자동화 실행 로그 보유

## 빠른 시작

1. Visual Studio 2022에서 `NetworkModuleTest.sln` 빌드
2. 서버 실행
   - `TestDBServer.exe -p 8001` (코드 기본값)
   - `TestServer.exe -p 9000 --db --db-host 127.0.0.1 --db-port 8001`
3. 클라이언트 실행
   - `TestClient.exe --host 127.0.0.1 --port 9000`

스크립트 실행 시 주의:
- `run_dbServer.ps1`, `run_server.ps1`, `run_allServer.ps1`의 DB 포트 기본값은 `8002`입니다.
- 코드 기본값(`8001`)과 다르므로 필요 시 명시적으로 `-DbPort 8001`을 지정하세요.

## 문서 진입점

- 전체 인덱스: `Doc/README.md`
- 아키텍처: `Doc/02_Architecture.md`
- 프로토콜: `Doc/03_Protocol.md`
- API: `Doc/04_API.md`
- 개발/빌드: `Doc/05_DevelopmentGuide.md`, `Doc/06_SolutionGuide.md`
- 코드-문서 매핑: `Doc/07_VisualMap.md`

## 참고

`Doc/Architecture`, `Doc/Performance`, `Doc/Reports` 하위에는 이력/보고서 성격 문서가 포함되어 있습니다. 운영 기준은 상단의 핵심 문서를 우선 참조하세요.
