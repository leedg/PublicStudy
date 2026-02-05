# API (현재 구현 기준)

## 1. 런타임 메시지
### 1.1 TestClient <-> TestServer
- SessionConnectReq: clientVersion
- SessionConnectRes: sessionId, serverTime, result
- PingReq: clientTime, sequence
- PongRes: clientTime, serverTime, sequence

### 1.2 TestServer <-> TestDBServer
- MessageHandler 포맷, MessageType Ping/Pong

## 2. CLI 옵션
### TestServer
- -p <port> (기본 9000)
- -d <connstr> (DB 연결 문자열, 옵션)
- -l <level> (DEBUG/INFO/WARN/ERROR)
- -h (도움말)

> 참고: `-d` 옵션은 `ENABLE_DATABASE_SUPPORT` 미정의 시 무시됩니다.

### TestDBServer
- -p <port> (기본 8002)
- -m <max> (기본 1000)
- -h (도움말)

### TestClient
- --host <addr> (기본 127.0.0.1)
- --port <port> (기본 9000)
- -l <level> (DEBUG/INFO/WARN/ERROR)
- -h (도움말)

## 3. 라이브러리 API 요약
### AsyncIOProvider
- Initialize(queueDepth, maxConcurrent)
- SendAsync / RecvAsync / ProcessCompletions

### INetworkEngine
- Initialize(maxConnections, port)
- Start / Stop
- SendData / CloseConnection

### Database 모듈 (ServerEngine)
- DatabaseFactory::CreateDatabase
- ConnectionPool::Initialize / GetConnection / ReturnConnection
