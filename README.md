# PublicStudy — NetworkModuleTest

**C++17 크로스플랫폼 네트워크 엔진 테스트베드**
Windows(IOCP/RIO) · Linux(epoll/io_uring) · macOS(kqueue) 백엔드를 단일 인터페이스로 추상화하고,
실제 클라이언트-서버 부하 환경에서 세션 관리 · 비동기 DB · 재연결 흐름을 검증합니다.

---

## 전체 아키텍처

<p align="center">
  <img src="NetworkModuleTest/Doc/WikiDraft/ServerStructure/assets/01-overall-architecture.png" alt="Overall Architecture" width="860"/>
</p>

> `TestClient → TestServer → TestDBServer(옵션)` 3-tier 구조.
> `ServerEngine`이 플랫폼별 IO 백엔드를 선택하고, `SessionManager`가 세션 생명주기를 관리합니다.

---

## 주요 특징

| 구분 | 내용 |
|------|------|
| **IO 백엔드** | Windows: IOCP / RIO · Linux: epoll / io_uring · macOS: kqueue |
| **세션 관리** | `SessionPool` + `KeyedDispatcher` (세션 키 기반 직렬화) |
| **비동기 DB** | `DBTaskQueue` → SQLite / ODBC / OLE DB 멀티 백엔드 |
| **재연결** | `TimerQueue` 기반 자동 DB 재연결 정책 |
| **버퍼 풀** | RIO slab pool · io_uring fixed buffer 사전 등록 |
| **Send 백프레셔** | 큐 임계치 초과 시 `SendResult::QueueFull` 반환 |
| **Linux 통합 테스트** | Docker 3-tier (DBServer → Server → Client) 자동화 |

---

## 세션 계층

<p align="center">
  <img src="NetworkModuleTest/Doc/WikiDraft/ServerStructure/assets/02-session-layer.png" alt="Session Layer" width="760"/>
</p>

---

## 패킷 · 비동기 DB 흐름

<p align="center">
  <img src="NetworkModuleTest/Doc/WikiDraft/ServerStructure/assets/03-packet-and-asyncdb-flow.png" alt="Packet and Async DB Flow" width="760"/>
</p>

---

## 플랫폼별 빠른 시작

### Windows (VS 2022)

> **선결 조건:** Visual Studio 2022 + C++ 빌드 도구 (MSVC v143) + Windows SDK 10.0

```powershell
cd NetworkModuleTest

# 빌드 (MSBuild 자동 탐지, PATH 등록 불필요)
.\build_all.ps1

# 서버 시작 (DBServer:18002 + Server:19010)
.\run_allServer.ps1

# 클라이언트 실행
.\run_client.ps1
```

### Linux — 네이티브

```bash
cd NetworkModuleTest

# 의존성 설치 (Ubuntu 22.04)
sudo apt-get install -y cmake build-essential pkg-config liburing-dev libsqlite3-dev

# io_uring + SQLite 포함 빌드
./scripts/build_unix.sh --enable-io-uring --enable-db

# 실행 (DBServer:9001 · Server:9000)
./build/DBServer -p 9001 &
./build/TestServer -p 9000 --db-host 127.0.0.1 --db-port 9001 &
./build/TestClient --host 127.0.0.1 --port 9000 --pings 5
```

### Linux — Docker (Windows 호스트)

> **선결 조건:** [Docker Desktop](https://www.docker.com/products/docker-desktop) (WSL2 백엔드)

```powershell
cd NetworkModuleTest

# epoll + io_uring 통합 테스트 (이미지 자동 빌드)
.\test_linux\run_docker_test.ps1
```

### macOS

```bash
cd NetworkModuleTest

# 의존성
xcode-select --install
brew install cmake pkg-config

# 빌드
./scripts/build_unix.sh --config Release
```

---

## 기본 포트

| 실행 파일 | Windows | Linux / macOS |
|-----------|---------|---------------|
| TestDBServer | `18002` | `9001` |
| TestServer | `19010` | `9000` |

> 포트 충돌 시 자동으로 다음 빈 포트로 fallback합니다. 고정하려면 `-DisablePortFallback`.

---

## 문서

| 문서 | 내용 |
|------|------|
| [프로젝트 개요](NetworkModuleTest/Doc/01_ProjectOverview.md) | 프로젝트 범위 · 현재 상태 · 변경 이력 |
| [아키텍처](NetworkModuleTest/Doc/02_Architecture.md) | 런타임 아키텍처 · 모듈 관계도 |
| [프로토콜](NetworkModuleTest/Doc/03_Protocol.md) | PacketDefine · ServerPacketDefine |
| [API](NetworkModuleTest/Doc/04_API.md) | CLI 옵션 · 주요 C++ API |
| [개발 가이드](NetworkModuleTest/Doc/05_DevelopmentGuide.md) | **플랫폼별 빌드/실행/테스트 상세** |
| [솔루션 구조](NetworkModuleTest/Doc/06_SolutionGuide.md) | 프로젝트 구성 · 빌드 순서 |
| [코드-문서 매핑](NetworkModuleTest/Doc/07_VisualMap.md) | 디렉터리 ↔ 문서 연결 지도 |

---

## 프로젝트 구조

```
NetworkModuleTest/
├── Server/
│   ├── ServerEngine/          # 핵심 엔진 (정적 라이브러리)
│   │   ├── Network/Core/      # INetworkEngine, Session, PacketDefine
│   │   ├── Network/Platforms/ # Windows · Linux · macOS 엔진 구현
│   │   ├── Concurrency/       # KeyedDispatcher, AsyncScope, TimerQueue
│   │   └── Database/          # IDatabase → SQLite / ODBC / OLE DB
│   ├── TestServer/            # 클라이언트 수락 서버
│   └── DBServer/              # 서버 간 패킷 처리 서버
├── Client/TestClient/         # 부하 · 연결 테스트 클라이언트
├── test_linux/                # Linux Docker 통합 테스트
├── scripts/                   # 플랫폼별 빌드 스크립트
└── Doc/                       # 설계 문서 및 개발 가이드
```

---

## 기술 스택

- **언어:** C++17
- **Windows 빌드:** MSBuild / Visual Studio 2022 (MSVC v143)
- **Linux · macOS 빌드:** CMake 3.15+ / GCC 12+
- **IO 백엔드:** WinSock2 RIO · IOCP · epoll · io_uring · kqueue
- **DB:** SQLite3 · ODBC (MSSQL / PostgreSQL / MySQL) · OLE DB
- **Linux 테스트:** Docker · docker-compose (Ubuntu 22.04)
