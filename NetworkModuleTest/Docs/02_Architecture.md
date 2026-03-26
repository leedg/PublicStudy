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
- TestServer: Windows `19010`, Linux/macOS `9000`
- TestDBServer 기본값: Windows `18002`, Linux/macOS `8001`
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
    TestServer/              # 클라이언트 수용 서버
    DBServer/                # 서버 간 패킷 처리 서버
  Client/TestClient/         # 테스트 클라이언트
  Docs/
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

## 5. TestClient <-> TestServer 흐름

1. `SessionConnectReq` 전송
2. `SessionConnectRes` 수신 (sessionId/result)
3. 주기적 `PingReq(clientTime, sequence)`
4. `PongRes(clientTime, serverTime, sequence)` 수신
5. RTT 통계 집계

중요
- 운영 경로의 Ping/Pong 와이어 포맷은 고정 필드 구조체입니다.
- `Tests/Protocols/PingPong.*`의 검증 페이로드(문자/숫자 에코)는 테스트 전용 경로입니다.

## 6. TestServer DB 처리 흐름

- 로컬 경로: `DBTaskQueue`가 Connect/Disconnect/PlayerData 작업을 비동기로 처리
- DB 인스턴스: `-d` 지정 시 SQLite, 미지정 시 Mock DB 사용
- 원격 DB 서버 경로(옵션): `--db` 또는 `--db-host/--db-port` 지정 시 TestDBServer와 패킷 교환
- DB ping 반복 작업은 `TimerQueue::ScheduleRepeat` 기반

## 7. 동시성/런타임 핵심

- `KeyedDispatcher`: key affinity 기반 직렬화
- `AsyncScope`: 세션 종료 시 예약된 작업 억제
- `TimerQueue`: 반복/지연 작업의 단일 워커 스케줄링
- `Session::SendResult`: `Ok/QueueFull/NotConnected/InvalidArgument`로 백프레셔 상태 명시
- `NetworkEventBus`: 채널 구독 기반 이벤트 브로드캐스트 인터페이스

## 8. 현재 주의 사항

- `BaseNetworkEngine::FireEvent`는 현재 구현상 콜백 맵 미등록 시 EventBus publish도 함께 건너뛸 수 있습니다.
- `SessionManager` 상한(`Utils::MAX_CONNECTIONS=1000`)과 `TestServer` 초기화 상수(`10000`)가 불일치합니다.
- 기본 포트는 플랫폼 define으로 분기됩니다. Windows는 19010/18002, Linux/macOS는 9000/8001입니다.
