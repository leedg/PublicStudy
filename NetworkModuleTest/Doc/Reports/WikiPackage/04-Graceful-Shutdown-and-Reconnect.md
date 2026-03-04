![DB Reconnect Sequence](assets/06-db-reconnect-sequence.svg)

![Graceful Shutdown Sequence](assets/05-graceful-shutdown-sequence.svg)

## 4.5 DB 서버 재연결 비동기 루프 (TestServer)

Windows 경로에서 TestServer는 DB 연결 끊김 시 재연결 스레드를 실행한다.

- 기본 지수 백오프: 1s -> 2s -> 4s ... max 30s
- 예외: `WSAECONNREFUSED(10061)`은 1초 고정 간격 재시도
- `condition_variable`로 `Stop()` 시 즉시 깨어나도록 설계

코드 포인트:

- 재연결 루프: `Server/TestServer/src/TestServer.cpp:583`
- 에러 구분: `.../TestServer.cpp:629`

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
