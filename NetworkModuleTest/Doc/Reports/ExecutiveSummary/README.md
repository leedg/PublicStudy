# Network / Async / DB 처리 구조 분석 보고서

- 작성일: 2026-02-26

- 기준 리포지토리: `NetworkModuleTest`

- 분석 기준: `ServerEngine`, `TestServer`, `DBServer` 실제 구현 코드

- 목적: 네트워크 처리, 비동기 처리, DB 처리의 현재 구조와 데이터 흐름을 코드 기준으로 정리

## Key Diagrams

![Architecture Overview](assets/01-architecture-overview.svg)

![Async DB Flow Sequence](assets/04-async-db-flow-sequence.svg)

![Graceful Shutdown Sequence](assets/05-graceful-shutdown-sequence.svg)

## 1. 요약

이 프로젝트는 `INetworkEngine`(상위 추상화)와 `AsyncIOProvider`(하위 플랫폼 백엔드)로 분리된 2계층 구조를 사용한다.

- 네트워크 처리: 플랫폼별 엔진(Windows/Linux/macOS)이 수락/완료 이벤트를 처리하고, 공통 로직은 `BaseNetworkEngine`이 담당
- 비동기 처리: I/O 워커 스레드 + 로직 스레드풀 + DB 전용 작업큐(`DBTaskQueue`, `OrderedTaskQueue`)로 역할 분리
- DB 처리: `IDatabase` 추상화 위에 `SQLite/Mock/ODBC` 구현을 두고, TestServer는 비동기 큐 기반으로 DB 작업을 오프로딩

핵심 특징:

- Windows: RIO 우선, 실패 시 IOCP 폴백
- Linux: io_uring 우선, 실패 시 epoll 폴백
- macOS: kqueue
- `ClientSession`의 DB 기록은 논블로킹(`DBTaskQueue`)으로 처리
- 종료 시 `DBTaskQueue` 드레인 -> DB 해제 -> 네트워크 종료 순서로 graceful shutdown 수행


## 6.2 TestServer <-> TestDBServer

1. TestServer가 `ConnectToDBServer()`로 별도 소켓 연결(Windows)
2. `DBPingLoop`에서 `PKT_ServerPingReq` 주기 전송
3. TestDBServer가 `ServerPongRes` 응답
4. 주기적으로 `PKT_DBSavePingTimeReq` 전송/응답
5. 끊김 시 `DBReconnectLoop` 재시도

관련 코드:

- 연결/스레드 시작: `Server/TestServer/src/TestServer.cpp:264`
- ping 루프: `.../TestServer.cpp:548`
- DBServer 패킷 처리: `Server/DBServer/src/ServerPacketHandler.cpp:116`


## 7.1 TestServer

`Stop()`에서 아래 순서를 지킨다.

1. DB 재연결 루프 깨움 및 join
2. 연결 중 세션의 disconnect 기록 enqueue
3. `DBTaskQueue->Shutdown()`으로 큐 드레인
4. 로컬 DB disconnect
5. DB 서버 소켓 disconnect
6. 클라이언트 엔진 stop

코드 포인트:

- 전체 순서: `Server/TestServer/src/TestServer.cpp:165`


## 7.2 TestDBServer

1. 네트워크 엔진 stop
2. OrderedTaskQueue shutdown(남은 작업 처리)
3. ServerLatencyManager shutdown

코드 포인트:

- `Server/DBServer/src/TestDBServer.cpp:160`


## 8.1 강점

- 네트워크/로직/DB 비동기 경로 분리 명확
- 플랫폼 폴백 체인과 공통 추상화 정리됨
- `DBTaskQueue` WAL 복구로 종료/크래시 안정성 강화
- 재연결 정책(ECONNREFUSED 분리)으로 운영 복원력 확보


## 8.2 유의점

- `TestServer`의 DB 서버 소켓 경로는 Windows 전용 (`#ifdef _WIN32`)
- `TestDBServer` 기본 경로는 현재 DB 주입 없음 -> 영구 DB 저장은 비활성(log-only)
- TestDBServer 기본 포트(코드 기본 8001)와 실행 스크립트 포트(문서/스크립트에서 8002 사용 가능) 혼동 위험


## 9. 개선 제안

1. `TestDBServer`에도 설정 기반 DB 주입(`SetDatabase`) 경로를 main 옵션으로 연결
2. TestServer의 DB 서버 연결 경로를 플랫폼 공통 소켓/AsyncIO 경로로 통합
3. 운영 문서에 "기본 실행 경로(TestDBServer) vs 실험 경로(DBServer.cpp)"를 명시
4. 포트 기본값(8001/8002) 정책을 코드/스크립트/문서에서 단일화
