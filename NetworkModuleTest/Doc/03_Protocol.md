# 프로토콜 (현재 코드 기준)

## 1. 개요

- Client <-> TestServer: `Server/ServerEngine/Network/Core/PacketDefine.h`
- TestServer <-> TestDBServer: `Server/ServerEngine/Network/Core/ServerPacketDefine.h`
- 레거시 실험 포맷: `Server/DBServer/src/DBServer.cpp`의 MessageHandler

## 2. Client <-> TestServer (`PacketDefine.h`)

### 2.1 공통 헤더

```cpp
struct PacketHeader {
    uint16_t size; // 헤더 포함 전체 크기
    uint16_t id;   // PacketType
};
```

### 2.2 PacketType

- `0x0001` SessionConnectReq
- `0x0002` SessionConnectRes
- `0x0003` PingReq
- `0x0004` PongRes

### 2.3 패킷 본문

- `PKT_SessionConnectReq`: `clientVersion`
- `PKT_SessionConnectRes`: `sessionId`, `serverTime`, `result(ConnectResult)`
- `PKT_PingReq`: `clientTime`, `sequence`
- `PKT_PongRes`: `clientTime`, `serverTime`, `sequence`

### 2.4 상수

- `MAX_PACKET_SIZE = 4096`
- `RECV_BUFFER_SIZE = 8192`
- `SEND_BUFFER_SIZE = 8192`
- `PING_INTERVAL_MS = 5000`
- `PING_TIMEOUT_MS = 30000`

## 3. TestServer <-> TestDBServer (`ServerPacketDefine.h`)

### 3.1 공통 헤더

```cpp
struct ServerPacketHeader {
    uint16_t size;
    uint16_t id;
    uint32_t sequence;
};
```

### 3.2 ServerPacketType

- `ServerPingReq (1000)`, `ServerPongRes (1001)`
- `DBSavePingTimeReq (2000)`, `DBSavePingTimeRes (2001)`
- `DBQueryReq (2002)`, `DBQueryRes (2003)`

### 3.3 구현 상태

- 현재 실사용 경로는 `ServerPingReq`, `DBSavePingTimeReq` 중심입니다.
- `DBQueryReq/Res`는 타입 정의는 있으나 핸들러 적용 범위가 제한적입니다.

## 4. 레거시 MessageHandler 포맷 (`DBServer.cpp`)

```text
[type(uint32)][connection_id(uint64)][timestamp(uint64)][payload]
```

- `Ping = 1`
- `Pong = 2`
- `CustomStart = 1000`

이 경로는 실험/레거시 호환용으로 유지되며, 기본 런타임 기준 프로토콜은 `PacketDefine/ServerPacketDefine`입니다.

## 5. 테스트 전용 프로토콜 (PingPong.*)

`ServerEngine/Tests/Protocols/PingPong.h/.cpp`는 검증 페이로드(랜덤 숫자/문자 에코)를 포함한 별도 테스트 프로토콜을 제공합니다.

주의
- 이 검증 페이로드는 테스트 모듈 경로이며, 기본 TestClient/TestServer 운영 패킷 포맷과는 분리되어 있습니다.
