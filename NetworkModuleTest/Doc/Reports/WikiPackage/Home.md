# Network / Async / DB 처리 구조 분석 보고서
- 작성일: 2026-02-26
- 기준 리포지토리: `NetworkModuleTest`
- 분석 기준: `ServerEngine`, `TestServer`, `DBServer` 실제 구현 코드
- 목적: 네트워크 처리, 비동기 처리, DB 처리의 현재 구조와 데이터 흐름을 코드 기준으로 정리

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
