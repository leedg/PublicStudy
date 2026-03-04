# Network / Async / DB 처리 구조 분석 보고서
- 최초 작성: 2026-02-26 / 최종 업데이트: 2026-03-02
- 기준 리포지토리: `NetworkModuleTest`
- 분석 기준: `ServerEngine`, `TestServer`, `DBServer` 실제 구현 코드
- 목적: 네트워크 처리, 비동기 처리, DB 처리의 현재 구조와 데이터 흐름을 코드 기준으로 정리

## 최신 변경 사항 (2026-03-02)
- 비동기 고도화: KeyedDispatcher / TimerQueue / AsyncScope / Send 백프레셔 / NetworkEventBus
- AsyncScope 풀 재사용 버그 수정 (`Reset()` 추가)
- Linux Docker 통합 테스트 인프라: epoll + io_uring 양 백엔드 PASS (`test_linux/`)

## Pages

1. [01-Overall-Architecture.md](01-Overall-Architecture.md)

2. [02-Network-and-Session-Flow.md](02-Network-and-Session-Flow.md)

3. [03-Async-DB-Flow.md](03-Async-DB-Flow.md)

4. [04-Graceful-Shutdown-and-Reconnect.md](04-Graceful-Shutdown-and-Reconnect.md)

5. [05-Operational-Notes.md](05-Operational-Notes.md)

## Diagrams

- `assets/01-architecture-overview.svg`

- `assets/02-session-uml.svg`

- `assets/03-client-lifecycle-sequence.svg`

- `assets/04-async-db-flow-sequence.svg`

- `assets/05-graceful-shutdown-sequence.svg`

- `assets/06-db-reconnect-sequence.svg`
