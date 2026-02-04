# Protocol

## 1. Overview
- TCP binary protocol for TestClient <-> TestServer
- Internal MessageHandler format for TestServer <-> TestDBServer

## 2. PacketDefine.h (Client <-> TestServer)
### Header
```cpp
struct PacketHeader {
    uint16_t size; // total size including header
    uint16_t id;   // PacketType
};
```

### PacketType
- 0x0001 SessionConnectReq
- 0x0002 SessionConnectRes
- 0x0003 PingReq
- 0x0004 PongRes

### ConnectResult
- Success, VersionMismatch, ServerFull, Banned, Unknown

### Ping/Pong
- PKT_PingReq: clientTime, sequence
- PKT_PongRes: clientTime, serverTime, sequence

### Constants
- MAX_PACKET_SIZE = 4096
- RECV_BUFFER_SIZE = 8192
- PING_INTERVAL_MS = 5000
- PING_TIMEOUT_MS = 30000

## 3. MessageHandler format (TestServer <-> TestDBServer)
```
[type(uint32)][connection_id(uint64)][timestamp(uint64)][payload]
```

### MessageType
- Ping = 1
- Pong = 2
- CustomStart = 1000

## 4. Protobuf
PingPongHandler can use protobuf when HAS_PROTOBUF is defined. Default build uses binary format.
