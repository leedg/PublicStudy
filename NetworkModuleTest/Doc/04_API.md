# API (현재 코드 기준)

## 1. 실행 파일 CLI

### 1.1 TestServer

- `-p <port>`: 서버 포트 (기본: Windows `19010`, Linux/macOS `9000`)
- `-d <path>`: 로컬 DB 경로 (지정 시 SQLite, 미지정 시 Mock)
- `--db`: 원격 DB 서버 연결 시도 (기본: Windows `127.0.0.1:18002`, Linux/macOS `127.0.0.1:8001`)
- `--db-host <addr>`: 원격 DB 서버 주소
- `--db-port <port>`: 원격 DB 서버 포트
- `-l <level>`: `DEBUG|INFO|WARN|ERROR`
- `-h, --help`: 도움말

### 1.2 TestDBServer

- `-p <port>`: 서버 포트 (기본: Windows `18002`, Linux/macOS `8001`)
- `-l <level>`: `DEBUG|INFO|WARN|ERROR`
- `-h, --help`: 도움말

참고
- `run_*.ps1` 실행 스크립트는 기본 포트 충돌 시 자동으로 다음 빈 포트를 사용합니다.

### 1.3 TestClient

- `--host <addr>`: 서버 주소 (기본 `127.0.0.1`)
- `--port <port>`: 서버 포트 (기본: Windows `19010`, Linux/macOS `9000`)
- `--pings <n>`: n회 ping 후 종료 (`0`=무제한)
- `--clients <n>`: 현재 무시되는 옵션 (단일 연결 모드)
- `-l <level>`: `DEBUG|INFO|WARN|ERROR`
- `-h, --help`: 도움말

## 2. 라이브러리 API 요약

### 2.1 INetworkEngine (`Network/Core/NetworkEngine.h`)

- `Initialize(maxConnections, port)`
- `Start()`, `Stop()`, `IsRunning()`
- `RegisterEventCallback(event, callback)`
- `SendData(connectionId, data, size)`
- `CloseConnection(connectionId)`
- `GetStatistics()`

### 2.2 엔진 팩토리

- `CreateNetworkEngine(engineType)`
  - 권장값: `"auto"`
  - 플랫폼별 명시값: `iocp/rio`, `epoll/io_uring`, `kqueue`

주의
- 헤더 기본 인자(`"asyncio"`)와 팩토리 구현 기대값(`auto/default/...`)이 불일치하므로, 호출 시 `"auto"`를 명시하는 것을 권장합니다.

### 2.3 Session 전송 결과

- `Session::Send(...) -> SendResult`
  - `Ok`: 큐 등록 성공
  - `QueueFull`: 백프레셔/풀 소진
  - `NotConnected`: 연결 상태 아님

### 2.4 TimerQueue (`Concurrency/TimerQueue.h`)

- `Initialize()`
- `ScheduleOnce(callback, delayMs)`
- `ScheduleRepeat(callback, intervalMs)`
- `Cancel(handle)`
- `Shutdown()`

### 2.5 NetworkEventBus (`Network/Core/NetworkEventBus.h`)

- `Publish(event, data)`
- `Subscribe(event, channel)` -> `SubscriberHandle`
- `Unsubscribe(handle)`

## 3. 현재 동작 범위 메모

- TestServer의 원격 DB 서버 연결(`ConnectToDBServer`)은 Windows 경로 중심으로 구현되어 있습니다.
- TestDBServer의 DB 저장은 일부 플레이스홀더/프로토타입 경로를 포함합니다.
