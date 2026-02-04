# API (Current Implementation)

## 1. Runtime messages
### 1.1 TestClient <-> TestServer
- SessionConnectReq: clientVersion
- SessionConnectRes: sessionId, serverTime, result
- PingReq: clientTime, sequence
- PongRes: clientTime, serverTime, sequence

### 1.2 TestServer <-> TestDBServer
- MessageHandler format, MessageType Ping/Pong

## 2. CLI options
### TestServer
- -p <port> (default 9000)
- -d <connstr> (optional DB connection string)
- -l <level> (DEBUG/INFO/WARN/ERROR)
- -h (help)

### TestDBServer
- -p <port> (default 8002)
- -m <max> (default 1000)
- -h (help)

### TestClient
- --host <addr> (default 127.0.0.1)
- --port <port> (default 9000)
- -l <level> (DEBUG/INFO/WARN/ERROR)
- -h (help)

## 3. Library API summary
### AsyncIOProvider
- Initialize(queueDepth, maxConcurrent)
- SendAsync / RecvAsync / ProcessCompletions

### INetworkEngine
- Initialize(maxConnections, port)
- Start / Stop
- SendData / CloseConnection

### Database Module (ServerEngine)
- DatabaseFactory::CreateDatabase
- ConnectionPool::Initialize / GetConnection / ReturnConnection
