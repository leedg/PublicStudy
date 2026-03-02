# 아키텍처

## 1. 목표
비동기 네트워크 엔진과 테스트 서버/클라이언트를 통해 핵심 네트워크/패킷/DB 흐름을 검증합니다.

## 2. 시스템 구성
TestClient -> TestServer -> TestDBServer (옵션)
      |           |
      +-----------+ ServerEngine

기본 포트
- TestServer: 9000
- TestDBServer: 8001 (코드 기본값) / 8002 (실행 스크립트 `run_dbServer.ps1` 등 기본값)

## 3. 디렉터리 구조
```text
NetworkModuleTest/
  Doc/
  Server/
    ServerEngine/
      Core/
        Memory/               # 플랫폼 독립 버퍼 풀 모듈 (2026-03-01 신규)
          IBufferPool.h
          StandardBufferPool.h/.cpp
          RIOBufferPool.h/.cpp
          IOUringBufferPool.h/.cpp
      Concurrency/
        ExecutionQueue.h      # Mutex/LockFree 선택 큐
        KeyedDispatcher.h     # key affinity 디스패처
        Channel.h             # 타입 메시지 채널
        AsyncScope.h          # 협력 취소 scope
        TimerQueue.h/.cpp     # min-heap 타이머 (2026-03-02 신규)
      Network/Core/
        SendBufferPool.h/.cpp # IOCP 전송 풀 (2026-03-01)
        NetworkEventBus.h/.cpp# 다중 구독자 이벤트 버스 (2026-03-02 신규)
      Platforms/Windows|Linux|macOS/
      Database/
      Implementations/Protocols/
      Tests/Protocols/
        PingPong.h/.cpp       # 검증 페이로드 포함 (2026-03-01)
      Utils/
    TestServer/
    DBServer/
  Client/TestClient/
  ModuleTest/
    DBModuleTest/
    MultiPlatformNetwork/
```

## 4. ServerEngine 구성
- INetworkEngine + BaseNetworkEngine: 공통 로직 (이벤트, 통계, 세션 관리)
- Platform NetworkEngine: Windows(IOCP/RIO), Linux(epoll/io_uring), macOS(kqueue)
- AsyncIOProvider: 플랫폼별 저수준 백엔드
- **Core/Memory**: `IBufferPool` 인터페이스 + `StandardBufferPool` / `RIOBufferPool` / `IOUringBufferPool` 구현체 (2026-03-01 신규)
- Session/SessionManager: 연결 및 세션 관리
- SendBufferPool: IOCP 경로 zero-copy 전송 버퍼 싱글턴
- PacketDefine: SessionConnect/Ping/Pong 바이너리 프레이밍
- Database: ConnectionPool, ODBC/OLEDB 구현
- Concurrency: ExecutionQueue, KeyedDispatcher, Channel, AsyncScope, BoundedLockFreeQueue, **TimerQueue** (상세: `Doc/03_ConcurrencyRuntime.md`)
- **NetworkEventBus**: `Network/Core/NetworkEventBus.h/.cpp` — 다중 구독자 이벤트 버스 싱글턴 (2026-03-02 신규)
- Utils: Logger, Timer, ThreadPool 등

## 5. Client <-> Server 플로우
1. Client가 TCP 연결 후 SessionConnectReq 전송
2. Server가 SessionConnectRes로 sessionId 전달
3. Client가 주기적으로 PingReq 전송 (검증 페이로드 포함)
4. Server가 PongRes로 응답 (검증 페이로드 에코)
5. Client가 에코된 검증 페이로드를 원본과 대조 (`GetLastValidationResult()`)

### PingPong 검증 페이로드 (2026-03-01)
Ping/Pong 패킷 와이어 포맷에 검증 필드 추가:
```
Ping: [uint64 timestamp][uint32 seq][string msg]
      [uint8 numCount][uint32×N nums][uint8 charCount][char×M chars]
Pong: [uint64 timestamp][uint64 pingTs][uint32 pingSeq][string msg]
      [uint8 numCount][uint32×N nums][uint8 charCount][char×M chars]  ← 에코
```
- `numCount` / `charCount` : 각 1~5개 랜덤 (`mt19937`)
- 문자 범위: 출력 가능 ASCII (0x21~0x7E)
- `ParsePong()` 에서 에코값과 원본 비교 → `mLastValidationOk` 설정

## 6. DBServer 연동
- TestServer <-> TestDBServer는 `ServerPacketDefine` 기반(Ping/Pong, DBSavePingTime)
- MessageHandler 포맷은 `DBServer.cpp` 실험 경로에서만 사용(기본 실행 경로 아님)
- TestServer의 DBTaskQueue/DB 풀은 `ENABLE_DATABASE_SUPPORT` 정의 시 활성 (현재는 로그/플레이스홀더)
- TestDBServer는 Ping/DBSavePingTime 패킷 처리 가능 (DB 저장은 플레이스홀더)
- `DBPingTimeManager` → `ServerLatencyManager` 통합 완료: `SavePingTime` / `GetLastPingTime` 메서드가 `ServerLatencyManager`로 이전됨
- `ClientSession` 의존성 주입: `static sDBTaskQueue` 전역 제거 → 생성자 주입(`mDBTaskQueue`), `TestServer::MakeClientSessionFactory()` 람다 패턴
- `DBTaskQueue` 워커 수: 2 → **1** (같은 세션 RecordConnect/Disconnect 순서 보장)
- `OrderedTaskQueue` (TestDBServer 전용): serverId 기반 해시 스레드 친화도 — 같은 serverId의 작업은 항상 동일 워커 스레드에서 순서대로 실행 (`Concurrency::KeyedDispatcher` 래핑)

## 7. 비동기 로직 고도화 이력 (2026-03-02)

| 영역 | 변경 내용 |
|------|---------|
| **LogicDispatcher** | `BaseNetworkEngine::mLogicThreadPool` → `KeyedDispatcher mLogicDispatcher` 교체. `Session::mRecvMutex` 제거 (key-affinity 직렬화 보장) |
| **TimerQueue** | `Concurrency/TimerQueue` 신규. DB ping 스레드 루프 → `ScheduleRepeat` 교체. 세션 타임아웃 체크도 타이머로 관리 |
| **AsyncScope** | `Session::mAsyncScope` 추가. `Close()` 시 `Cancel()` 호출 → 이미 큐에 들어간 recv 작업 억제 |
| **Send 백프레셔** | `Session::Send()` → `SendResult` (Ok/QueueFull/NotConnected) 반환. `SEND_QUEUE_BACKPRESSURE_THRESHOLD=64` (소프트 임계값) |
| **NetworkEventBus** | `Network/Core/NetworkEventBus` 신규 싱글턴. `BaseNetworkEngine::FireEvent()` 에서 `Publish()` 호출 → 다중 구독자 동시 이벤트 수신 가능 |

## 8. 제약 및 향후 과제
- Linux/macOS 경로는 기본 send/recv 구현 완료 (테스트/안정성 검증 필요)
- TestServer ↔ TestDBServer 패킷 처리 연결 강화 필요
- DB CRUD 실연동 필요
- TLS/인증/압축 미구현
