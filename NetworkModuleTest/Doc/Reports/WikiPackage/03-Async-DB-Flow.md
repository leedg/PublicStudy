![Async DB Flow Sequence](assets/04-async-db-flow-sequence.svg)

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
