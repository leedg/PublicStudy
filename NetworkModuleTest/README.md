# NetworkModuleTest

**C++17 크로스플랫폼 네트워크 엔진 테스트베드**

Windows(IOCP/RIO) · Linux(epoll/io_uring) · macOS(kqueue) 백엔드를
단일 인터페이스(`INetworkEngine`)로 추상화합니다.
실제 클라이언트-서버 부하 환경에서 **세션 관리 · 비동기 DB · 재연결 흐름**을 검증합니다.

> **Wiki:** https://github.com/leedg/PublicStudy/wiki

---

## 주요 특징

| 구분 | 내용 |
|------|------|
| **IO 백엔드** | Windows: IOCP / RIO &nbsp;·&nbsp; Linux: epoll / io_uring &nbsp;·&nbsp; macOS: kqueue |
| **세션 관리** | `SessionPool` + `KeyedDispatcher` (세션 키 기반 직렬화, 락 없는 순서 보장) |
| **비동기 DB** | `DBTaskQueue` → SQLite / ODBC / OLE DB / PostgreSQL 멀티 백엔드 |
| **자동 재연결** | 지수 백오프(1s→30s max) · `condition_variable` 즉시 깨우기 설계 |
| **버퍼 풀** | RIO slab pool · io_uring fixed buffer 사전 등록 (Non-Paged Pool 절약) |
| **Send 백프레셔** | 큐 임계치 초과 시 `SendResult::QueueFull` 반환 |
| **Linux 통합 테스트** | Docker 3-tier (DBServer → Server → Client) 자동화 · 결과 git 자동 저장 |

---

## 플랫폼별 빠른 시작

### Windows — VS 2022

> **선결 조건:** Visual Studio 2022 + C++ 빌드 도구(MSVC v143) + Windows SDK 10.0

```powershell
# 빌드
.\build_all.ps1

# 서버 시작 (DBServer:8002, Server:9000)
.\run_all_servers.ps1

# 클라이언트 실행
.\run_client.ps1
```

### Linux — 네이티브

> **선결 조건 (Ubuntu 22.04):**

```bash
sudo apt-get install -y cmake build-essential pkg-config liburing-dev libsqlite3-dev
```

```bash
# io_uring + SQLite 포함 빌드
./scripts/build_unix.sh --enable-io-uring --enable-db

# 실행 (DBServer:9001, Server:9000)
./build/DBServer -p 9001 &
./build/TestServer -p 9000 --db-host 127.0.0.1 --db-port 9001 &
./build/TestClient --host 127.0.0.1 --port 9000 --pings 5
```

### Linux — Docker (Windows 호스트)

> **선결 조건:** [Docker Desktop](https://www.docker.com/products/docker-desktop) (WSL2 백엔드)

```powershell
# epoll + io_uring 통합 테스트 (이미지 자동 빌드)
.\test_linux\run_docker_test.ps1

# 결과를 git에 자동 커밋/푸시
.\test_linux\run_docker_test.ps1 -Push
```

### macOS

> **선결 조건:** `xcode-select --install` + `brew install cmake pkg-config`

```bash
# 전체 빌드 (TestServer, DBServer, TestClient)
./scripts/build_unix.sh --config Release
```

---

## 기본 포트

| 실행 파일 | 포트 |
|-----------|:----:|
| TestDBServer | `8002` |
| TestServer   | `9000` |

---

## 문서

### 핵심 문서

| 문서 | 내용 |
|------|------|
| [01 프로젝트 개요](Docs/01_ProjectOverview.md) | 프로젝트 범위 · 현재 상태 · 변경 이력 |
| [02 아키텍처](Docs/02_Architecture.md) | 런타임 구조 · 모듈 관계 · 플랫폼별 엔진 |
| [08 프로토콜](Docs/08_Protocol.md) | PacketDefine · ServerPacketDefine 바이너리 포맷 |
| [04 API](Docs/04_API.md) | CLI 옵션 · 주요 C++ API 레퍼런스 |
| [05 개발 가이드](Docs/05_DevelopmentGuide.md) | 플랫폼별 빌드 · 실행 · 테스트 상세 |
| [06 솔루션 가이드](Docs/06_SolutionGuide.md) | 솔루션/프로젝트 구성 · 빌드 순서 |
| [07 코드-문서 매핑](Docs/07_VisualMap.md) | 디렉터리 ↔ 문서 연결 지도 |

### Wiki (GitHub)

아키텍처 · 모듈 상세 설명은 GitHub Wiki를 참고하세요.

| 페이지 | 내용 |
|--------|------|
| [전체 구조](https://github.com/leedg/PublicStudy/wiki/01-Overall-Architecture) | 2계층 구조, 플랫폼 분기 |
| [네트워크 엔진](https://github.com/leedg/PublicStudy/wiki/02-Network-Engine) | AsyncIOProvider, 플랫폼 백엔드 |
| [세션 계층](https://github.com/leedg/PublicStudy/wiki/03-Session-Layer) | Session · SessionManager · SessionPool |
| [동시성 런타임](https://github.com/leedg/PublicStudy/wiki/04-Concurrency) | ExecutionQueue · KeyedDispatcher · AsyncScope |
| [데이터베이스](https://github.com/leedg/PublicStudy/wiki/05-Database) | IDatabase · ConnectionPool · 멀티DB |
| [버퍼/메모리](https://github.com/leedg/PublicStudy/wiki/06-Buffer-Memory) | IBufferPool · RIO/IOUring/Standard |
| [종료 및 재연결](https://github.com/leedg/PublicStudy/wiki/07-Shutdown-Reconnect) | Graceful Shutdown · DB 재연결 백오프 |
| [빌드 및 실행](https://github.com/leedg/PublicStudy/wiki/08-Build-and-Run) | 빌드 명령 · 실행 순서 · 포트 정보 |

### 보고서 · 성능

| 문서 | 내용 |
|------|------|
| [Executive Summary](Docs/Reports/ExecutiveSummary/) | 요약 보고서 (다이어그램 포함) |
| [팀 공유 보고서](Docs/Reports/TeamShare/) | 팀 공유용 상세 보고서 |
| [성능 테스트 로그](Docs/Performance/Logs/) | Windows · Linux 벤치마크 결과 |

### DB 모듈 테스트

| 문서 | 내용 |
|------|------|
| [DBModuleTest 가이드](ModuleTest/DBModuleTest/Docs/README.md) | DB 백엔드 테스트 전체 가이드 |
| [빠른 참조](ModuleTest/DBModuleTest/Docs/README_SHORT.md) | 한 페이지 요약 |

---

## 프로젝트 구조

```
NetworkModuleTest/
├── Server/
│   ├── ServerEngine/              # 핵심 엔진 (정적 라이브러리)
│   │   ├── Network/Core/          # INetworkEngine · Session · PacketDefine
│   │   ├── Network/Platforms/     # Windows(IOCP/RIO) · Linux(epoll/io_uring) · macOS(kqueue)
│   │   ├── Concurrency/           # KeyedDispatcher · AsyncScope · TimerQueue
│   │   └── Database/              # IDatabase → SQLite / ODBC / OLE DB / PostgreSQL
│   ├── TestServer/                # 클라이언트 수락 서버
│   └── DBServer/                  # 서버 간 패킷 처리 서버
│
├── Client/TestClient/             # 부하 · 연결 테스트 클라이언트
│
├── ModuleTest/
│   └── DBModuleTest/              # DB 모듈 독립 테스트 (5 백엔드)
│
├── test_linux/                    # Linux Docker 통합 테스트
│   ├── Dockerfile
│   ├── docker-compose.yml
│   └── scripts/
│
├── scripts/                       # 플랫폼별 빌드 스크립트 (크로스플랫폼 통합)
│   ├── build_windows.ps1          # Windows 전체 빌드 (옵션 풍부)
│   ├── build_unix.sh              # Linux / macOS 빌드
│   ├── validation/                # 구조 검증 스크립트
│   └── db_tests/                  # DB 백엔드별 테스트
│
├── build_all.ps1                  # Windows 빠른 빌드 (thin wrapper)
│
├── Docs/                          # 설계 문서 및 개발 가이드
│   ├── Wiki/                      # GitHub Wiki 소스 (main push 시 자동 동기화)
│   │   └── scripts/               # publish-wiki.ps1, pre-push.hook
│   ├── Architecture/              # 아키텍처 레퍼런스 문서
│   ├── Reports/                   # 보고서 패키지 (ExecutiveSummary, TeamShare 등)
│   └── Performance/               # 성능 분석 로그
│
└── dev-planning/                  # 작업 설계 문서 (플랜·스펙·AI 협업 기록)
    ├── plans/
    └── superpowers/
```

---

## 기술 스택

| 구분 | 내용 |
|------|------|
| **언어** | C++17 |
| **Windows 빌드** | MSBuild / Visual Studio 2022 (MSVC v143) |
| **Linux · macOS 빌드** | CMake 3.15+ / GCC 12+ / Clang |
| **IO 백엔드** | WinSock2 RIO · IOCP · Linux epoll · io_uring · BSD kqueue |
| **DB** | SQLite3 · ODBC (MSSQL / PostgreSQL / MySQL) · OLE DB |
| **Linux 테스트** | Docker · docker-compose · Ubuntu 22.04 |
