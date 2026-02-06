# 프로젝트 개요

## 요약
NetworkModuleTest는 ServerEngine을 중심으로 구성된 분산 서버 테스트베드입니다.
현재 기본 런타임 흐름은 TestClient -> TestServer -> TestDBServer(옵션)입니다.

## 핵심 구성 요소
- ServerEngine: AsyncIOProvider, IOCP 엔진, 공용 유틸, DB 모듈
- TestServer: IOCP 기반 서버, 패킷 수신/처리
- TestDBServer: Ping/Pong 메시지 처리 (네트워크 accept/send 미구현, DB CRUD 스텁)
- TestClient: SessionConnect + Ping/Pong + RTT 통계

## 상태 (2026-02-04)
| 모듈 | 상태 | 비고 |
| --- | --- | --- |
| ServerEngine | 진행 중 | 네트워크/DB 모듈 구현 |
| TestServer | 프로토타입 | 세션/핑 처리, DB 옵션(`ENABLE_DATABASE_SUPPORT`) |
| TestDBServer | 프로토타입 | Ping/Pong 처리 |
| TestClient | 프로토타입 | RTT 통계 포함 |
| MultiPlatformNetwork | 보관 | 참고 구현 |

## 최근 업데이트
- CLI 옵션/기본 포트를 코드 기준으로 정리
- PacketDefine/MessageHandler 기준으로 프로토콜 정리
