# 비동기 DB 아키텍처 (현재 실행 경로 기준)

이 문서는 `NetworkModuleTest`의 현재 코드 기준으로 `TestServer`의 비동기 DB 경로를 설명한다.
과거 문서에 있던 `MakeClientSessionFactory()` 중심 설명, `ClientSession::OnConnected()` 중심 설명,
그리고 "단일 공유 큐" 설명은 현재 기본 실행 경로와 다르다.

## 요약

- 세션 생성은 `SessionManager::CreateSession()`이 담당한다.
- 실제 세션 객체는 `SessionPool`에서 가져온 `Core::Session`이다.
- 세션별 recv 콜백은 `SessionManager::SetSessionConfigurator()`로 주입된다.
- 접속/종료 DB 기록은 현재 `TestServer`의 네트워크 이벤트 핸들러가 `DBTaskQueue`에 enqueue한다.
- `DBTaskQueue`는 워커별 독립 큐 구조를 가지며, `sessionId % workerCount`로 라우팅한다.
- 현재 `TestServer` 런타임 설정은 `Initialize(1, "db_tasks.wal", db)` 이다.
- 원격 DB 서버 ping 반복 작업은 `TimerQueue::ScheduleRepeat()`로 실행된다.

## 현재 런타임 흐름

```text
accept
  -> SessionManager::CreateSession(socket)
  -> SessionPool에서 Core::Session 획득
  -> Session::Initialize(id, socket)
  -> SessionConfigurator(SetOnRecv) 적용
  -> 첫 PostRecv()

Connected event
  -> TestServer::OnClientConnectionEstablished()
  -> DBTaskQueue::RecordConnectTime(sessionId, timestamp)

Disconnected event
  -> TestServer::OnClientConnectionClosed()
  -> DBTaskQueue::RecordDisconnectTime(sessionId, timestamp)
```

핵심은 "패킷 처리 경로"와 "DB 기록 경로"가 분리되어 있다는 점이다.
recv 콜백은 세션에 붙고, 접속/종료 기록은 엔진 이벤트를 받아 `TestServer`가 처리한다.

## 세션 생성과 설정

현재 기본 경로는 아래와 같다.

1. 플랫폼 엔진이 accept 후 `SessionManager::CreateSession()`을 호출한다.
2. `SessionManager`는 `SessionPool`에서 `Core::Session`을 획득한다.
3. 세션 초기화 후 `SetSessionConfigurator()`에 등록된 콜백을 적용한다.
4. 이 configurator가 `SetOnRecv()`를 등록해 첫 recv 이전에 패킷 핸들러가 붙도록 보장한다.

즉, 현재 기본 런타임은 "세션 팩토리로 `ClientSession` 생성" 구조가 아니다.

### `ClientSession`의 현재 위치

- `ClientSession` 클래스 자체는 아직 코드 트리에 남아 있다.
- `weak_ptr<DBTaskQueue>` 주입과 `AsyncRecordConnectTime()` 같은 보조 구현도 유지되어 있다.
- 다만 현재 기본 세션 할당 경로에서는 `ClientSession`을 생성하지 않는다.

문서에서 `ClientSession`을 언급할 때는 "현재 활성 런타임 경로"가 아니라
"참고용/레거시 보조 구현"인지 명확히 구분해야 한다.

## `DBTaskQueue` 구조

### 핵심 구조

`DBTaskQueue`는 내부적으로 `mWorkers`를 가진다.
각 워커는 아래 자원을 독립적으로 소유한다.

- `std::queue<DBTask> taskQueue`
- `std::mutex mutex`
- `std::condition_variable cv`
- `std::thread thread`

즉, 현재 구현은 "단일 공유 큐 + 단일 공유 CV"가 아니라
"워커별 독립 큐" 구조다.

### 라우팅 규칙

모든 작업은 아래 규칙으로 워커에 배정된다.

```text
workerIndex = sessionId % workerCount
```

이 규칙 때문에 같은 `sessionId`의 작업은 항상 같은 워커로 간다.
그리고 각 워커는 단일 스레드 + FIFO로 동작하므로,
같은 세션 내부에서는 작업 순서가 유지된다.

### 현재 설정과 구현 능력은 구분해야 한다

현재 `TestServer`는 아래처럼 워커 수 1로 초기화한다.

```cpp
mDBTaskQueue = std::make_shared<DBTaskQueue>();
mDBTaskQueue->Initialize(1, "db_tasks.wal", mLocalDatabase.get());
```

이것은 현재 런타임 설정이다.
하지만 구현 자체는 이미 "세션 친화도 기반 멀티워커"를 지원한다.

따라서 문서에서는 아래 두 문장을 분리해서 써야 한다.

- 현재 설정: `TestServer`는 workerCount=1
- 현재 구현: `DBTaskQueue`는 워커별 독립 큐 + 세션 친화도 라우팅

## 로컬 DB와 WAL

### 로컬 DB 선택

`TestServer::Initialize()`는 로컬 DB를 먼저 만든 뒤 `DBTaskQueue`에 주입한다.

- `dbConnectionString`이 비어 있으면 `MockDatabase`
- 값이 있으면 `SQLiteDatabase`

현재 기본 경로에서는 `DBTaskQueue`가 항상 이 로컬 DB를 통해
접속/종료/플레이어 데이터 작업을 처리한다.

### 보장되는 테이블

초기화 시 아래 테이블을 보장한다.

- `SessionConnectLog`
- `SessionDisconnectLog`
- `PlayerData`

### WAL 복구

`DBTaskQueue`는 `db_tasks.wal` 파일을 사용해 크래시 복구를 지원한다.

- enqueue 직전 `P|...` pending 기록
- 성공 처리 후 `D|seq` 기록
- 재시작 시 WAL 본문과 `.bak`를 함께 읽어 미완료 작업만 재큐잉

이 구조 덕분에 종료 중 장애나 비정상 종료가 있어도
완료되지 않은 작업을 다시 enqueue 할 수 있다.

## 접속/종료 기록 경로

### 접속 기록

현재 활성 경로는 아래와 같다.

1. 클라이언트 접속
2. 엔진이 `NetworkEvent::Connected` 발생
3. `TestServer::OnClientConnectionEstablished()` 호출
4. 현재 시각 문자열 생성
5. `DBTaskQueue::RecordConnectTime(sessionId, timestamp)` enqueue
6. 큐 워커가 나중에 DB INSERT 수행

### 종료 기록

정상 운영 중 종료 이벤트는 아래 흐름을 따른다.

1. 클라이언트 연결 종료
2. 엔진이 `NetworkEvent::Disconnected` 발생
3. `TestServer::OnClientConnectionClosed()` 호출
4. 서버가 아직 실행 중이면 `RecordDisconnectTime()` enqueue

서버 종료 중에는 중복 기록을 막기 위해 `OnClientConnectionClosed()` 경로는 건너뛰고,
`Stop()`이 세션 스냅샷을 떠서 disconnect 기록을 한 번 더 정리한다.

### 왜 `ClientSession::OnConnected()` 기준 문서가 틀리는가

현재 기본 경로에서는 접속/종료 기록이 `ClientSession`이 아니라
`TestServer` 이벤트 핸들러에서 발생한다.
따라서 아래 식의 설명은 더 이상 기준 문서가 될 수 없다.

- `ClientSession::OnConnected() -> AsyncRecordConnectTime()`
- `ClientSession::OnDisconnected() -> AsyncRecordDisconnectTime()`
- `MakeClientSessionFactory()`가 `DBTaskQueue`를 주입

## 패킷 처리와 DB 처리의 분리

현재 recv 처리 경로는 아래와 같다.

1. 세션에 `SetOnRecv()`로 패킷 콜백이 등록된다.
2. recv 완료 시 세션이 콜백을 통해 `ClientPacketHandler::ProcessPacket()`를 호출한다.
3. DB 접속/종료 기록은 이 경로와 별개로 `TestServer` 이벤트 핸들러가 처리한다.

즉, "패킷 처리"와 "DB 기록"은 서로 다른 진입점에서 시작한다.

## 종료 순서

현재 `TestServer::Stop()`의 의도된 순서는 아래와 같다.

1. 재연결 루프를 깨우고 join
2. 아직 연결 중인 세션 스냅샷을 떠서 disconnect 기록 enqueue
3. `TimerQueue` 종료
4. `DBTaskQueue->Shutdown()`으로 큐 드레인
5. 로컬 DB disconnect
6. 원격 DB 서버 소켓 disconnect
7. 네트워크 엔진 stop

이 순서를 지켜야 큐에 남은 DB 작업이 살아 있는 DB 연결 위에서 마무리된다.

## 원격 DB 서버 ping 경로

과거의 `DBPingLoop` 스레드는 제거되었다.
현재는 `ConnectToDBServer()`에서 `TimerQueue::ScheduleRepeat()`를 등록해
주기적으로 `SendDBPing()`을 호출한다.

문서에서 아래 표현은 더 이상 쓰지 않는다.

- "`DBPingLoop`에서 ping 전송"
- "별도 ping 스레드가 sleep 기반으로 반복"

대신 아래 표현을 사용한다.

- "`TimerQueue::ScheduleRepeat()`가 DB ping을 스케줄링"

## 현재 문서화 규칙

앞으로 `DBTaskQueue` 관련 문서를 쓸 때는 아래 규칙을 따른다.

1. 현재 설정과 구현 능력을 구분한다.
2. `SessionFactory` 대신 `SetSessionConfigurator()`를 기준으로 설명한다.
3. 접속/종료 DB 기록은 `TestServer` 이벤트 핸들러 기준으로 설명한다.
4. `DBPingLoop` 대신 `TimerQueue` 기준으로 설명한다.
5. `ClientSession`은 "현재 활성 경로"인지 "참고용 구현"인지 명시한다.

## 관련 코드

- `Server/ServerEngine/Network/Core/SessionManager.h/.cpp`
- `Server/ServerEngine/Network/Core/SessionPool.h/.cpp`
- `Server/TestServer/include/TestServer.h`
- `Server/TestServer/src/TestServer.cpp`
- `Server/TestServer/include/DBTaskQueue.h`
- `Server/TestServer/src/DBTaskQueue.cpp`
- `Server/TestServer/include/ClientSession.h`
- `Server/TestServer/src/ClientSession.cpp`

검증일: 2026-03-15
