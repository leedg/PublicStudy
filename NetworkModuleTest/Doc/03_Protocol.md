# 프로토콜

## 1. 개요
- TCP 바이너리 프로토콜: TestClient <-> TestServer (`PacketDefine.h`)
- 서버 간 패킷: TestServer <-> TestDBServer (`ServerPacketDefine.h`, 부분 구현)
- 레거시 메시지 포맷: DBServer.cpp의 MessageHandler (실험/레거시)

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

## 3. ServerPacketDefine.h (TestServer <-> TestDBServer, 부분 구현)

### 공통 헤더
```cpp
struct ServerPacketHeader {
    uint16_t size;      // 전체 크기
    uint16_t id;        // ServerPacketType
    uint32_t sequence;  // 시퀀스
};
```

### ServerPacketType
- ServerPingReq (1000), ServerPongRes (1001)
- DBSavePingTimeReq (2000), DBSavePingTimeRes (2001)
- DBQueryReq (2002), DBQueryRes (2003)

### 비고
- TestServer는 `DBServerPacketHandler`를 통해 Ping/DBSave 요청을 생성
- TestDBServer는 현재 수신 로그 중심이며 실제 처리 경로 보강 필요

## 4. MessageHandler 포맷 (레거시, DBServer.cpp)
```
[type(uint32)][connection_id(uint64)][timestamp(uint64)][payload]
```

### MessageType
- Ping = 1
- Pong = 2
- CustomStart = 1000

## 5. Protobuf (테스트)
- `ServerEngine/Tests/Protocols/PingPong.*`에서 HAS_PROTOBUF 정의 시 protobuf 메시지 사용
- 기본 빌드는 바이너리 포맷 사용
