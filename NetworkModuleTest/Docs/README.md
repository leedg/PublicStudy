# NetworkModuleTest 문서 인덱스

이 문서는 현재 코드 기준으로 유지되는 핵심 문서와, 이력/참조 문서를 구분해 안내합니다.

## 1. 핵심 문서 (우선 참조)

1. `01_ProjectOverview.md` - 프로젝트 범위, 현재 상태, 최근 변경 요약
2. `02_Architecture.md` - 런타임 아키텍처와 모듈 관계
3. `03_ConcurrencyRuntime.md` - 동시성 런타임 (ExecutionQueue, KeyedDispatcher)
4. `04_API.md` - CLI 옵션 및 주요 C++ API 요약
5. `05_DevelopmentGuide.md` - 빌드/실행/테스트 가이드
6. `06_SolutionGuide.md` - 솔루션/프로젝트 구성
7. `07_VisualMap.md` - 코드 디렉터리와 문서 매핑
8. `08_Protocol.md` - PacketDefine/ServerPacketDefine 프로토콜

## 2. 참고 문서 (설계 이력/분석)

- `Architecture/*` - 플랫폼/구조 설계 이력
- `Network/*` - 네트워크 구현 분석 자료
- `Database/*` - DB 모듈 관련 문서
- `Performance/*` - 성능 실험 기록 및 로그
- `Reports/*` - 공유용 패키지(Executive/TeamShare/Wiki)

참고 문서는 작성 시점 기준 정보가 포함될 수 있습니다. 현재 구현 동작 확인은 핵심 문서 + 코드(`Server/`, `Client/`)를 기준으로 판단하세요.

## 3. 현재 기준 핵심 사실

- TestServer 기본 포트: `9000`
- TestDBServer 기본 포트: `8002`
- PowerShell 실행 스크립트는 포트 충돌 시 자동으로 다음 빈 포트로 이동
- TestClient 기본 연결: Windows `127.0.0.1:19010`, Linux/macOS `127.0.0.1:9000`
- 클라이언트 Ping/Pong 와이어 포맷은 고정 필드(`clientTime`, `sequence`, `serverTime`) 기반

## 4. 문서 갱신 원칙

- 코드 우선: 문서와 코드가 다르면 코드 동작을 기준으로 문서를 수정
- 범위 명시: 테스트 전용 경로와 운영 경로를 분리 표기
- 기본값 명시: 플랫폼별 기본값(Windows vs Linux/macOS)과 스크립트 자동 fallback 정책을 함께 기록
