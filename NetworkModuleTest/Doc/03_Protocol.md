# 프로토콜

## 1. 개요
- TCP 바이너리 프로토콜: TestClient <-> TestServer
- 내부 메시지 포맷: TestServer <-> TestDBServer

## 2. PacketDefine.h (Client <-> TestServer)
### 공통 헤더
```cpp
struct PacketHeader {
    uint16_t size; // 헤더 포함 전체 크기
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

### 상수
- MAX_PACKET_SIZE = 4096
- RECV_BUFFER_SIZE = 8192
- PING_INTERVAL_MS = 5000
- PING_TIMEOUT_MS = 30000

## 3. MessageHandler 포맷 (TestServer <-> TestDBServer)
```
[type(uint32)][connection_id(uint64)][timestamp(uint64)][payload]
```

### MessageType
- Ping = 1
- Pong = 2
- CustomStart = 1000

## 4. Protobuf
PingPongHandler는 HAS_PROTOBUF 정의 시 protobuf 메시지를 사용합니다.
기본 빌드는 바이너리 포맷을 사용합니다.
