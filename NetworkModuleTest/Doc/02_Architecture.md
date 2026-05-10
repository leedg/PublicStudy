# 아키텍처 (현재 코드 기준)

## 1. 목표

`ServerEngine` 기반 네트워크 런타임과 `TestServer/TestDBServer/TestClient`를 통해 연결, 패킷 처리, 비동기 작업, 종료/재연결 흐름을 검증합니다.

## 2. 런타임 토폴로지

```text
TestClient --(PacketDefine)--> TestServer --(ServerPacketDefine, 옵션)--> TestDBServer
                     |
                     +--> DBTaskQueue + Local DB(Mock/SQLite)
```

기본 포트
- TestServer: `19010`
- TestDBServer 기본값: `18002`
- PowerShell 실행 스크립트: 포트 충돌 시 다음 빈 포트로 자동 fallback

## 3. 주요 디렉터리

```text
NetworkModuleTest/
  Server/
    ServerEngine/
      Network/Core/          # INetworkEngine, BaseNetworkEngine, Session, PacketDefine
      Network/Platforms/     # Windows/Linux/macOS 엔진
      Concurrency/           # KeyedDispatcher, AsyncScope, TimerQueue, Channel
      Database/              # DB 추상화/구현
      Utils/                 # Logger, ConfigManager, Timer, CrashDump
    TestServer/              # 클라이언트 수용 서버
    DBServer/                # 서버 간 패킷 처리 서버
  Client/TestClient/         # 테스트 클라이언트
  Doc/
```

## 4. ServerEngine 계층

- `INetworkEngine`: 공통 엔진 인터페이스
- `BaseNetworkEngine`: 세션/이벤트/통계 공통 로직
- 플랫폼 구현
  - Windows: IOCP/RIO
  - Linux: epoll/io_uring
  - macOS: kqueue
- `SessionManager`/`SessionPool`: 세션 할당/수명관리
- `PacketProcessor`: 수신 패킷 라우팅

## 5. 설정 관리 (ConfigManager)

`ConfigManager`는 중앙 설정 레지스트리로, 모든 서버 실행 시 기본값을 로드하고 환경 변수로 덮어쓸 수 있습니다.

### 설정 구조

```cpp
struct NetworkConfig {
    uint16_t ListenPort = 19010;
    uint16_t DBServerPort = 18002;
    std::string DBServerHost = "127.0.0.1";
    std::string EngineType = "auto";
    size_t MaxConnections = 1000;
    size_t MaxLogicQueueDepth = 10000;
    uint32_t WorkerThreadCount = 0; // 0 = auto
};

struct TimeoutConfig {
    uint32_t ConnectTimeoutMs = 5000;
    uint32_t RecvTimeoutMs = 30000;
    uint32_t PingIntervalMs = 5000;
    uint32_t PingTimeoutMs = 10000;
    uint32_t GracefulShutdownTimeoutMs = 8000;
    // Docker 컨테이너는 짧은 타임아웃 자동 적용 (DOCKER_CONTAINER=1)
};

struct LogConfig {
    std::string Level = "INFO";
    std::string LogDir = "./logs";
    bool EnableFile = false;
};
```

### 환경 변수

| 변수 | 설명 | 기본값 |
|------|------|--------|
| `NETMOD_LISTEN_PORT` | 서버侦听端口 | 19010 |
| `NETMOD_DB_HOST` | DB服务器主机 | 127.0.0.1 |
| `NETMOD_DB_PORT` | DB服务器端口 | 18002 |
| `NETMOD_ENGINE` | 网络引擎 (auto/rio/iocp/epoll/kqueue) | auto |
| `NETMOD_WORKER_THREADS` | Worker线程数 (0=auto) | 0 |
| `NETMOD_LOG_LEVEL` | 日志级别 (DEBUG/INFO/WARN/ERROR) | INFO |
| `NETMOD_GRACEFUL_TIMEOUT` | 正常关机超时(秒) | 8 |

## 6. TestClient <-> TestServer 흐름

1. `SessionConnectReq` 전송
2. `SessionConnectRes` 수신 (sessionId/result)
3. 주기적 `PingReq(clientTime, sequence)`
4. `PongRes(clientTime, serverTime, sequence)` 수신
5. RTT 통계 집계

重要
- 운영 경로의 Ping/Pong 와이어 포맷은 고정 필드 구조체입니다.
- `Tests/Protocols/PingPong.*`의 검증 페이로드(문자/숫자 에코)는 테스트 전용 경로입니다.

## 7. TestServer DB 처리 흐름

- 로컬 경로: `DBTaskQueue`가 Connect/Disconnect/PlayerData 작업을 비동기로 처리
- DB 인스턴스: `-d` 지정 시 SQLite, 미지정 시 Mock DB 사용
- 원격 DB 서버 경로(옵션): `--db` 또는 `--db-host/--db-port` 지정 시 TestDBServer와 패킷 교환
- DB ping 반복 작업은 `TimerQueue::ScheduleRepeat` 기반

## 8. 동시성/런타임 핵심

- `KeyedDispatcher`: key affinity 기반 직렬화
- `AsyncScope`: 세션 종료 시 예약된 작업 억제
- `TimerQueue`: 반복/지연 작업의 단일 워커 스케줄링
- `Session::SendResult`: `Ok/QueueFull/NotConnected`로 백프레셔 상태 명시
- `NetworkEventBus`: 채널 구독 기반 이벤트 브로드캐스트 인터페이스

## 9. 현재 주의 사항

- `BaseNetworkEngine::FireEvent`는 현재 구현상 콜백 맵 미등록 시 EventBus publish도 함께 건너뛸 수 있습니다.
- `SessionManager` 상한(`Utils::MAX_CONNECTIONS=1000`)과 `TestServer` 초기화 상수(`10000`)가 불일치합니다.
- 기본 포트는 `ConfigManager`의 `NetworkConfig`에서 관리됩니다. Docker 환경에서는 환경 변수로 덮어쓰기 가능.
