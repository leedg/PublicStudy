# NetworkModuleTest

High-performance async network module plus test servers/clients for a distributed server architecture.

## Overview
- Goal: Validate ServerEngine-based network and DB modules with TestServer/TestDBServer/TestClient.
- Core: AsyncIOProvider, IOCPNetworkEngine, Session/Packet, Database module.

### Architecture (runtime flow)
TestClient -> TestServer -> TestDBServer (optional)
      |           |
      +-----------+ ServerEngine (AsyncIO, Utils, Database, Protocols)

### Project layout
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

## Status (2026-02-04)

| Module | Status | Notes |
| --- | --- | --- |
| ServerEngine | In progress | IOCP engine, AsyncIOProvider, Database module. Linux/macOS provider sources included. |
| TestServer | Prototype | IOCP accept/session/ping. Optional DB pool with compile flag. |
| TestDBServer | Prototype | AsyncIOProvider + MessageHandler + Ping/Pong. DB CRUD is stub. |
| TestClient | Prototype | SessionConnect + Ping/Pong + RTT stats. |
| DBModuleTest | Legacy/test | Standalone DB test module. New code should use ServerEngine/Database. |
| MultiPlatformNetwork | Done/archived | Cross-platform AsyncIO reference. |

## Tech stack
- C++17
- Visual Studio 2022 (primary)
- CMake (partial, MultiPlatformNetwork focused)
- Async I/O: IOCP/RIO, epoll/io_uring, kqueue
- Database: ODBC/OLEDB (module implementation)
- Serialization: Protobuf (optional, Ping/Pong handler)

## Quick start (Windows)
1. Open `NetworkModuleTest.sln` and build x64 Debug/Release.
2. Run `TestDBServer.exe -p 8002`.
3. Run `TestServer.exe -p 9000` (optional DB: `-d "<connstr>"`).
4. Run `TestClient.exe --host 127.0.0.1 --port 9000`.
5. Auto run: `run_test.ps1` or `run_test.bat`.

## Docs
- `Doc/ProjectOverview.md`
- `Doc/Architecture.md`
- `Doc/Protocol.md`
- `Doc/API.md`
- `Doc/Development.md`
- `Doc/DevelopmentGuide.md`
- `Doc/SolutionGuide.md`
- `Server/ServerEngine/Database/README.md`
- `ModuleTest/DBModuleTest/README.md`

## Next steps
1. Implement real accept/send for TestDBServer.
2. Expand TestServer packet handling and DBServer integration.
3. Stabilize Protobuf path and test targets.
4. Align/clean CMake scripts (currently partial).
