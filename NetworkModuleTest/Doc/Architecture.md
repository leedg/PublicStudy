# Architecture

## 1. Goal
Provide an async server engine and test servers/clients to validate core network, packet, and DB behavior.

## 2. System layout
TestClient -> TestServer -> TestDBServer (optional)
      |           |
      +-----------+ ServerEngine

Default ports
- TestServer: 9000
- TestDBServer: 8002

## 3. Directory structure
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

## 4. ServerEngine components
- AsyncIOProvider: platform backends (IOCP/RIO, epoll/io_uring, kqueue)
- IOCPNetworkEngine: Windows IOCP server implementation
- Session/SessionManager: connection/session lifecycle
- PacketDefine: binary framing for SessionConnect/Ping/Pong
- Database: ConnectionPool, ODBC/OLEDB implementations
- Utils: Logger, Timer, ThreadPool, etc.

## 5. Client <-> Server flow
1. Client connects and sends SessionConnectReq
2. Server replies SessionConnectRes with sessionId
3. Client sends PingReq periodically
4. Server replies PongRes

## 6. DBServer integration
- TestServer <-> TestDBServer uses MessageHandler (Ping/Pong)
- DB CRUD messaging is planned but not implemented

## 7. Gaps / future work
- Real socket accept/send for TestDBServer
- Packet handlers for more message types
- TLS/auth/compression (not implemented)
