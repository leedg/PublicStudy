# PublicStudy

C++ 멀티플랫폼 네트워크 서버 엔진 학습 및 구현 프로젝트입니다.

모든 실질적인 코드와 문서는 [`NetworkModuleTest/`](./NetworkModuleTest/) 하위에 위치합니다.

---

## 디렉토리 구조

```
NetworkModuleTest/
├── Server/               # 서버 핵심 코드 (정본)
│   ├── ServerEngine/     # 네트워크 엔진 라이브러리
│   ├── TestServer/       # 게임 로직 테스트 서버
│   ├── DBServer/         # DB 연동 서버
│   └── Tests/            # 플랫폼별 통합 테스트 (RIO, io_uring)
│
├── Client/
│   └── TestClient/       # 부하 테스트 및 핑퐁 클라이언트
│
├── ModuleTest/
│   ├── DBModuleTest/         # DB 모듈 단독 테스트 (ODBC, OLE DB)
│   ├── MultiPlatformNetwork/ # 플랫폼별 AsyncIO Provider 단독 테스트
│   └── ServerStructureSync/  # 서버 구조 동기화 검증 스크립트
│
└── Doc/                  # 설계 문서 및 개발 가이드
    ├── Architecture/     # 아키텍처 설계 문서
    ├── Network/          # 네트워크 모듈 API/코딩 규칙
    ├── Database/         # DB 모듈 마이그레이션 가이드
    ├── Development/      # 빌드, 네이밍, 유닛테스트 가이드
    ├── Performance/      # 성능 분석 및 벤치마크 결과
    ├── Reports/          # 기술 보고서 패키지
    └── WikiDraft/        # GitHub Wiki 초안
```

---

## Server/ServerEngine 주요 구성

| 경로 | 설명 |
|------|------|
| `Network/Core/` | Session, SessionManager, NetworkEngine, AsyncIOProvider |
| `Network/Platforms/` | Windows / Linux / macOS 네트워크 엔진 구현 |
| `Platforms/Windows/` | IOCP, RIO AsyncIO Provider |
| `Platforms/Linux/` | epoll, io_uring AsyncIO Provider |
| `Platforms/macOS/` | kqueue AsyncIO Provider |
| `Concurrency/` | ThreadPool, ExecutionQueue, KeyedDispatcher 등 |
| `Database/` | ConnectionPool, ODBC/OLE DB/SQLite 구현 |
| `Interfaces/` | IDatabase, IConnection, IMessageHandler 등 인터페이스 |
| `Utils/` | Logger, Timer, SafeQueue, BufferManager 등 |

---

## 빠른 시작

```powershell
# 전체 서버 실행 (TestServer + DBServer)
./NetworkModuleTest/run_allServer.ps1

# 클라이언트 실행
./NetworkModuleTest/run_client.ps1

# 자동 테스트 (2 클라이언트)
./NetworkModuleTest/run_test_auto.ps1
```

---

## 관련 문서

- [프로젝트 개요](./NetworkModuleTest/Doc/01_ProjectOverview.md)
- [아키텍처 설계](./NetworkModuleTest/Doc/02_Architecture.md)
- [API 레퍼런스](./NetworkModuleTest/Doc/04_API.md)
- [개발 가이드](./NetworkModuleTest/Doc/05_DevelopmentGuide.md)
