# Network / Async / DB 처리 구조 분석 보고서

- 작성일: 2026-02-26
- 기준 리포지토리: `NetworkModuleTest`
- 분석 기준: `ServerEngine`, `TestServer`, `DBServer` 실제 구현 코드
- 목적: 네트워크 처리, 비동기 처리, DB 처리의 현재 구조와 데이터 흐름을 코드 기준으로 정리

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

## 2. 분석 범위

### 2.1 네트워크 엔진

- `Server/ServerEngine/Network/Core/NetworkEngine.h`
- `Server/ServerEngine/Network/Core/BaseNetworkEngine.h`
- `Server/ServerEngine/Network/Core/BaseNetworkEngine.cpp`
- `Server/ServerEngine/Network/Core/NetworkEngineFactory.cpp`
- `Server/ServerEngine/Network/Core/AsyncIOProvider.h`
- `Server/ServerEngine/Network/Core/AsyncIOProvider.cpp`
- `Server/ServerEngine/Network/Platforms/WindowsNetworkEngine.cpp`
- `Server/ServerEngine/Network/Platforms/LinuxNetworkEngine.cpp`
- `Server/ServerEngine/Network/Platforms/macOSNetworkEngine.cpp`
- `Server/ServerEngine/Network/Core/Session.h`
- `Server/ServerEngine/Network/Core/Session.cpp`
- `Server/ServerEngine/Network/Core/SessionManager.cpp`

### 2.2 TestServer (게임 서버)

- `Server/TestServer/src/TestServer.cpp`
- `Server/TestServer/src/ClientSession.cpp`
- `Server/TestServer/src/ClientPacketHandler.cpp`
- `Server/TestServer/src/DBTaskQueue.cpp`
- `Server/TestServer/src/DBServerPacketHandler.cpp`
- `Server/TestServer/main.cpp`

### 2.3 DBServer

- `Server/DBServer/src/TestDBServer.cpp`
- `Server/DBServer/src/ServerPacketHandler.cpp`
- `Server/DBServer/src/OrderedTaskQueue.cpp`
- `Server/DBServer/src/ServerLatencyManager.cpp`
- `Server/DBServer/main.cpp`
- 참고(대체/실험 경로): `Server/DBServer/src/DBServer.cpp`

## 3. 네트워크 처리 구조

## 3.1 계층 구조

`INetworkEngine`가 외부 API를 정의하고(`Initialize/Start/Stop/SendData/CloseConnection`), `BaseNetworkEngine`이 공통 동작을 구현한다.

- 공통: 세션 조회/제거, 이벤트 콜백 등록, 통계 집계
- 플랫폼 특화: 소켓 생성, accept 루프, completion 처리

코드 포인트:

- 인터페이스: `Server/ServerEngine/Network/Core/NetworkEngine.h:72`
- 공통 구현: `Server/ServerEngine/Network/Core/BaseNetworkEngine.cpp:28`

## 3.2 플랫폼 백엔드 선택

엔진 팩토리(`CreateNetworkEngine("auto")`)와 AsyncIO 팩토리가 폴백 체인을 제공한다.

- Windows: RIO -> IOCP
  - `Server/ServerEngine/Network/Core/NetworkEngineFactory.cpp:24`
  - `Server/ServerEngine/Network/Core/AsyncIOProvider.cpp:74`
- Linux: io_uring -> epoll
  - `Server/ServerEngine/Network/Core/NetworkEngineFactory.cpp:58`
  - `Server/ServerEngine/Network/Core/AsyncIOProvider.cpp:104`
- macOS: kqueue
  - `Server/ServerEngine/Network/Core/NetworkEngineFactory.cpp:105`

## 3.3 연결 수립부터 세션 생성까지

플랫폼 엔진 `AcceptLoop()` 공통 흐름:

1. `accept()`로 소켓 수락
2. `SessionManager::CreateSession()`로 세션 생성
3. `AsyncIOProvider::AssociateSocket()`으로 백엔드 연동
4. 로직 스레드풀에서 `OnConnected` + `Connected 이벤트` 실행
5. 첫 `Recv` 등록

Windows 기준 코드:

- accept 루프: `Server/ServerEngine/Network/Platforms/WindowsNetworkEngine.cpp:128`
- 소켓 associate: `.../WindowsNetworkEngine.cpp:167`
- 연결 이벤트 비동기 실행: `.../WindowsNetworkEngine.cpp:188`

## 3.4 수신 처리와 패킷 재조립

수신 완료 처리(`ProcessCompletions`)에서 `Recv` 완료를 `BaseNetworkEngine::ProcessRecvCompletion()`으로 전달한다.

- 데이터는 로직 스레드풀로 전달된 후 `Session::ProcessRawRecv()` 실행
- `ProcessRawRecv()`는 TCP 스트림을 `PacketHeader(size,id)` 기준으로 재조립
- 유효하지 않은 크기/오버플로우 탐지 시 세션 종료

코드 포인트:

- completion 처리: `Server/ServerEngine/Network/Platforms/WindowsNetworkEngine.cpp:233`
- 공통 recv 완료: `Server/ServerEngine/Network/Core/BaseNetworkEngine.cpp:255`
- TCP 재조립: `Server/ServerEngine/Network/Core/Session.cpp:445`

## 3.5 송신 처리

송신 경로는 플랫폼별로 나뉜다.

- Windows + RIO: `Session::Send()`에서 provider `SendAsync()` 직행
- 그 외(WSASend/epoll/kqueue): `mSendQueue`에 enqueue 후 `PostSend()`로 비동기 송신

`Session`은 `mSendQueueSize`(atomic)와 `mIsSending`(CAS)로 락 경쟁을 줄이고 중복 송신을 방지한다.

코드 포인트:

- 송신 진입: `Server/ServerEngine/Network/Core/Session.cpp:155`
- 큐 플러시/CAS: `.../Session.cpp:248`
- 실제 PostSend: `.../Session.cpp:261`

## 3.6 이벤트 스레드 일관성

`OnDisconnected`는 `CloseConnection()` 경로와 recv 오류 경로 모두 로직 스레드풀로 전달되어 실행된다. 즉, 콜백 호출 스레드가 일관되게 유지된다.

코드 포인트:

- `CloseConnection` 경로: `Server/ServerEngine/Network/Core/BaseNetworkEngine.cpp:155`
- recv 오류 경로: `.../BaseNetworkEngine.cpp:264`

## 4. 비동기 처리 구조

## 4.1 공통 스레드풀/큐

- `ThreadPool`은 내부적으로 `SafeQueue`를 사용
- `Submit()` 시 큐가 가득 차면 작업 드롭 후 경고 로그
- 워커에서 예외를 삼켜 프로세스 전체 실패를 방지

코드 포인트:

- `Server/ServerEngine/Utils/ThreadPool.h:22`
- `Server/ServerEngine/Utils/SafeQueue.h:18`

## 4.2 TestServer의 DBTaskQueue (논블로킹 DB)

`ClientSession`은 접속/해제 시점을 직접 DB에 쓰지 않고 `DBTaskQueue`에 작업을 enqueue한다.

- `OnConnected` -> `AsyncRecordConnectTime` -> `RecordConnectTime`
- `OnDisconnected` -> `AsyncRecordDisconnectTime` -> `RecordDisconnectTime`

코드 포인트:

- `ClientSession` 비동기 기록: `Server/TestServer/src/ClientSession.cpp:32`, `:76`, `:114`
- 큐 enqueue: `Server/TestServer/src/DBTaskQueue.cpp:206`
- 워커 실행: `.../DBTaskQueue.cpp:321`

중요 설계:

- `Initialize(1)`로 워커 1개를 강제하여 같은 sessionId 작업 순서 보장
- 멀티워커 필요 시 `OrderedTaskQueue` 전환을 주석과 로그로 안내

코드 포인트:

- 초기화 및 경고: `Server/TestServer/src/DBTaskQueue.cpp:48`
- TestServer에서 1워커 사용: `Server/TestServer/src/TestServer.cpp:102`

## 4.3 WAL 기반 복구

`DBTaskQueue`는 WAL(`db_tasks.wal`)로 크래시 복구를 지원한다.

- enqueue 전 `P|...`(pending) 기록
- 성공 처리 후 `D|seq` 기록
- 재시작 시 WAL(+백업 `.bak`)을 병합 파싱해 미완료 작업만 재큐잉

코드 포인트:

- pending/done 기록: `Server/TestServer/src/DBTaskQueue.cpp:538`, `:574`
- 복구 로직: `.../DBTaskQueue.cpp:597`

## 4.4 OrderedTaskQueue (DBServer)

`TestDBServer`는 serverId 단위 순서 보장을 위해 `OrderedTaskQueue`를 사용한다.

- 내부적으로 `KeyedDispatcher` 사용
- 같은 key(serverId)는 같은 worker queue로 라우팅
- worker FIFO로 key 단위 순서 보장

코드 포인트:

- facade: `Server/DBServer/src/OrderedTaskQueue.cpp:29`
- keyed dispatch: `.../OrderedTaskQueue.cpp:109`
- dispatcher 구현: `Server/ServerEngine/Concurrency/KeyedDispatcher.h:30`

## 4.5 DB 서버 재연결 비동기 루프 (TestServer)

Windows 경로에서 TestServer는 DB 연결 끊김 시 재연결 스레드를 실행한다.

- 기본 지수 백오프: 1s -> 2s -> 4s ... max 30s
- 예외: `WSAECONNREFUSED(10061)`은 1초 고정 간격 재시도
- `condition_variable`로 `Stop()` 시 즉시 깨어나도록 설계

코드 포인트:

- 재연결 루프: `Server/TestServer/src/TestServer.cpp:583`
- 에러 구분: `.../TestServer.cpp:629`

## 5. DB 처리 구조

## 5.1 공통 DB 추상화 계층

DB 레이어는 `IDatabase` / `IStatement` 인터페이스 기반으로 구현체를 교체 가능하게 설계되어 있다.

- 구현체: `MockDatabase`, `SQLiteDatabase`, `ODBCDatabase`, `OLEDBDatabase`
- 생성: `DatabaseFactory::CreateDatabase()`

코드 포인트:

- 인터페이스: `Server/ServerEngine/Interfaces/IDatabase.h:29`
- 팩토리: `Server/ServerEngine/Database/DatabaseFactory.cpp:19`

## 5.2 TestServer 로컬 DB 경로

`TestServer::Initialize()`에서 로컬 DB를 선택한다.

- `dbConnectionString` 비어있음: `MockDatabase`
- 값 있음: `SQLiteDatabase` + 해당 파일 경로

선택된 DB 인스턴스를 `DBTaskQueue`에 주입하고, 큐에서 아래 테이블을 보장한다.

- `SessionConnectLog`
- `SessionDisconnectLog`
- `PlayerData`

코드 포인트:

- DB 선택/주입: `Server/TestServer/src/TestServer.cpp:81`
- 큐 테이블 보장: `Server/TestServer/src/DBTaskQueue.cpp:73`

## 5.3 TestDBServer DB 관련 처리

`TestDBServer`는 `ServerPacketHandler` + `ServerLatencyManager` + `OrderedTaskQueue` 조합으로 동작한다.

- `ServerPingReq` 수신 시 RTT 계산 후 `RecordLatency`
- `DBSavePingTimeReq` 수신 시 `SavePingTime` 실행 후 응답 패킷 전송

코드 포인트:

- 핸들러 라우팅: `Server/DBServer/src/ServerPacketHandler.cpp:40`
- RTT 기록: `.../ServerPacketHandler.cpp:116`
- ping 저장: `.../ServerPacketHandler.cpp:196`

현재 상태(중요):

- `TestDBServer` 경로에는 `ServerLatencyManager::SetDatabase()` 주입 코드가 없음
- `ServerLatencyManager::ExecuteQuery()`는 DB 미주입 시 true 반환(log-only)

즉, 현재 기본 실행 경로에서는 메모리 통계/로그 중심이며, 영구 저장은 DB 주입이 연결되어야 활성화된다.

코드 포인트:

- TestDBServer 초기화: `Server/DBServer/src/TestDBServer.cpp:68`
- DB 미주입 시 log-only: `Server/DBServer/src/ServerLatencyManager.cpp:331`

## 5.4 대체/실험 경로: DBServer.cpp

`DBServer.cpp` 구현은 별도 AsyncIO/Protocol 처리 경로를 가지며, 여기서는 `ConnectToDatabase()`에서 `mLatencyManager->SetDatabase()`를 수행한다.

코드 포인트:

- DB 연결/주입: `Server/DBServer/src/DBServer.cpp:246`, `:281`

실행 엔트리(`Server/DBServer/main.cpp`)는 현재 `TestDBServer`를 사용하므로, 운영 경로와 실험 경로를 문서에서 구분해 보는 것이 안전하다.

## 6. 엔드투엔드 흐름

## 6.1 Client -> TestServer

1. 클라이언트 TCP 접속
2. 엔진 accept 후 Session 생성/associate
3. `ClientSession::OnConnected()` 호출
4. DBTaskQueue에 접속 시간 기록 enqueue
5. 클라이언트 `SessionConnectReq` 처리 후 `SessionConnectRes` 반환
6. 주기적 PingReq/PongRes

관련 코드:

- accept: `Server/ServerEngine/Network/Platforms/WindowsNetworkEngine.cpp:128`
- 접속 기록: `Server/TestServer/src/ClientSession.cpp:32`
- 핸드셰이크 처리: `Server/TestServer/src/ClientPacketHandler.cpp:102`

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

## 7. Graceful Shutdown 순서

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

## 8. 현재 상태 진단

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

## 10. 부록: 주요 참조 파일

- `Server/ServerEngine/Network/Core/BaseNetworkEngine.cpp`
- `Server/ServerEngine/Network/Core/Session.cpp`
- `Server/ServerEngine/Network/Platforms/WindowsNetworkEngine.cpp`
- `Server/ServerEngine/Network/Platforms/LinuxNetworkEngine.cpp`
- `Server/TestServer/src/TestServer.cpp`
- `Server/TestServer/src/ClientSession.cpp`
- `Server/TestServer/src/DBTaskQueue.cpp`
- `Server/TestServer/src/ClientPacketHandler.cpp`
- `Server/DBServer/src/TestDBServer.cpp`
- `Server/DBServer/src/ServerPacketHandler.cpp`
- `Server/DBServer/src/OrderedTaskQueue.cpp`
- `Server/DBServer/src/ServerLatencyManager.cpp`
- `Server/DBServer/src/DBServer.cpp` (대체/실험 경로)

---

## 11. 업데이트 이력 (보고서 작성 이후)

### 11.1 2026-02-28 — RIO slab pool 도입 (WSA 10055 수정)

**문제**: `RIOAsyncIOProvider`가 소켓 연결마다 `RIORegisterBuffer`를 호출하여
Windows Non-Paged Pool을 소진 → 1000 클라이언트에서 WSA 10055 (`ENOBUFS`) 발생

**변경 내용**:

| 파일 | 변경 내용 |
|------|-----------|
| `RIOAsyncIOProvider.h/.cpp` | per-I/O 등록 폐지, `Initialize()`에서 recv·send 2개 slab 사전 등록 (VirtualAlloc + RIORegisterBuffer 각 1회) |
| `NetworkTypes.h` | `MAX_CONNECTIONS = 1000` (10000 → 1000) |
| `WindowsNetworkEngine.cpp` | CQ 깊이 = `effectiveMax * 2 + 64` 동적 계산 |

**결과**: 1000/1000 연결 PASS, 오류 0 (x64 Release, 퍼포먼스 테스트 2회 확인)

---

### 11.2 2026-03-01 — 메모리 풀 3단계 추가 최적화

**배경**: RIO slab pool 도입 이후 남아있던 IOCP 경로 및 공용 풀 비효율 3곳 개선

#### 변경 1: AsyncBufferPool O(1) 프리리스트

**파일**: `Platforms/AsyncBufferPool.h/.cpp`

- 기존: `Acquire()` / `Release()` 모두 O(n) 선형 탐색
- 변경: `vector<size_t> mFreeIndices` 스택(O(1) pop/push) + `unordered_map<int64_t,size_t> mBufferIdToIndex`(O(1) 조회)

#### 변경 2: Session::ProcessRawRecv 배치 버퍼

**파일**: `Network/Core/Session.cpp`

- 기존: 완성 패킷마다 `vector<char>` 생성 → N 패킷 = N 힙 alloc
- 변경: 단일 패킷 패스트패스 (0 alloc) + 일반 경로 배치 평탄 버퍼 (1 alloc)

#### 변경 3: SendBufferPool (IOCP 전송 버퍼 풀)

**파일**: `Network/Core/SendBufferPool.h/.cpp` (신규)

- 기존 IOCP Send 경로: `vector<char>` per-send 힙 alloc + 2회 memcpy
- 변경: 풀 슬롯 Acquire(O(1)) → 1회 memcpy → wsaBuf 포인터 직접 설정(zero-copy WSASend)

#### 퍼포먼스 테스트 결과 (2026-03-01)

`run_perf_test.ps1 -Phase all -RampClients @(10,100,500,1000) -SustainSec 30 -BinMode Release`

| 단계 | 목표 | 실제 | 오류 | Server WS | 판정 |
|------|------|------|------|-----------|------|
| 10   | 10   | 10   | 0    | 178.9 MB  | **PASS** |
| 100  | 100  | 100  | 0    | 180.6 MB  | **PASS** |
| 500  | 500  | 500  | 0    | 188 MB    | **PASS** |
| 1000 | 1000 | 1000 | 0    | 193.7 MB  | **PASS** |

> 상세 로그: `Doc/Performance/Logs/20260301_111832/`
> 누적 이력: `Doc/Performance/Logs/PERF_HISTORY.md`
