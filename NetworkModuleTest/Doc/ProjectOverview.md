# Project Overview

## Summary
NetworkModuleTest is a distributed server testbed built around ServerEngine.
The primary runtime flow is TestClient -> TestServer -> TestDBServer (optional).

## Key components
- ServerEngine: async IO abstraction, IOCP network engine, utilities, database module.
- TestServer: IOCP-based server for client connections and packet handling.
- TestDBServer: prototype server that responds to Ping/Pong via MessageHandler.
- TestClient: connects, performs SessionConnect, sends Ping, tracks RTT.

## Status (2026-02-04)
| Module | Status | Notes |
| --- | --- | --- |
| ServerEngine | In progress | Core network + DB module implemented. |
| TestServer | Prototype | Accept/session/ping implemented. DB optional. |
| TestDBServer | Prototype | Ping/Pong, DB CRUD stub. |
| TestClient | Prototype | Ping/Pong + stats. |
| MultiPlatformNetwork | Archived | Reference implementation. |

## Update notes
- CLI options and default ports updated to match code.
- PacketDefine/MessageHandler references aligned with current sources.
