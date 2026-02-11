# 프로젝트 개요

## 요약
NetworkModuleTest는 ServerEngine을 중심으로 구성된 분산 서버 테스트베드입니다.
현재 기본 런타임 흐름은 TestClient → TestServer → TestDBServer(옵션)이며,
DB 연동은 프로토타입(로그/플레이스홀더 중심) 단계입니다.

## 핵심 구성 요소
- ServerEngine: INetworkEngine/BaseNetworkEngine, AsyncIOProvider, Session/SessionManager, 공용 유틸, DB 모듈(ODBC/OLEDB)
- TestServer: NetworkEngine 기반 테스트 서버, 패킷 수신/처리, DBTaskQueue(비동기 작업)
- TestDBServer: NetworkEngine 기반 DB 서버 프로토타입, Ping/Pong 및 PingTime 기록 (DB 저장은 플레이스홀더)
- TestClient: SessionConnect + Ping/Pong + RTT 통계
- DBServer (DBServer.cpp): AsyncIOProvider 기반 실험/레거시 구현 (기본 실행 경로 아님)
- MultiPlatformNetwork: 보관 | 참고 구현

## 상태 (2026-02-10)
| 모듈 | 상태 | 비고 |
| --- | --- | --- |
| ServerEngine | 진행 중 | Windows 중심, Linux/macOS 기본 I/O 경로 구현 (테스트 필요) |
| TestServer | 프로토타입 | 세션/핑 처리, DB 옵션(`ENABLE_DATABASE_SUPPORT`) |
| TestDBServer | 프로토타입 | Ping/Pong 및 PingTime 기록, DB 저장은 플레이스홀더 |
| TestClient | 프로토타입 | RTT 통계 포함 |
| MultiPlatformNetwork | 보관 | 참고 구현 |

## 최근 업데이트
- TestServer ↔ TestDBServer 패킷 연결 경로 추가
- Linux/macOS 기본 I/O 경로 보강
- 문서-코드 정합성 점검 및 기본 포트/옵션 갱신
