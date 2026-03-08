# NetworkModuleTest

C++17 기반의 네트워크 테스트베드입니다. 현재 기본 런타임 흐름은 `TestClient -> TestServer -> TestDBServer(옵션)` 이며, ServerEngine의 플랫폼별 네트워크 엔진(Windows/Linux/macOS)을 검증하는 목적에 맞춰 구성되어 있습니다.

## 현재 상태 (2026-03-05 기준)

- 기본 클라이언트-서버 프로토콜: `SessionConnectReq/Res`, `PingReq/PongRes` (고정 바이너리 구조체)
- TestServer 로컬 DB 경로: `DBTaskQueue + Mock/SQLite`
- TestServer <-> TestDBServer 경로: `ServerPacketDefine` 기반 (Ping/Pong, DBSavePingTime 중심)
- Linux 통합 테스트: `test_linux/` 경로에서 epoll/io_uring 자동화 실행 로그 보유

---

## 플랫폼별 빠른 시작

### Windows (VS 2022)

**선결 조건:** Visual Studio 2022 + C++ 빌드 도구 (v143) + Windows SDK 10.0

```powershell
git clone <repo-url>
cd NetworkModuleTest

# 빌드 (MSBuild 자동 탐지, PATH 등록 불필요)
.\build_all.ps1

# 서버 + 클라이언트 실행
.\run_allServer.ps1   # DBServer(18002) + Server(19010) 자동 시작
.\run_client.ps1
```

### Linux (네이티브)

**선결 조건:** `sudo apt-get install -y cmake build-essential pkg-config liburing-dev libsqlite3-dev`

```bash
git clone <repo-url>
cd NetworkModuleTest

./scripts/build_unix.sh --enable-io-uring --enable-db

# 실행 (기본 포트: DBServer=9001, Server=9000)
./build/DBServer -p 9001 &
./build/TestServer -p 9000 --db-host 127.0.0.1 --db-port 9001 &
./build/TestClient --host 127.0.0.1 --port 9000 --pings 5
```

### Linux (Docker — Windows 호스트에서 실행)

**선결 조건:** Docker Desktop for Windows (WSL2 백엔드)

```powershell
git clone <repo-url>
cd NetworkModuleTest

# epoll + io_uring 통합 테스트 (이미지 자동 빌드)
.\test_linux\run_docker_test.ps1
```

### macOS

**선결 조건:** `xcode-select --install` + `brew install cmake pkg-config`

```bash
git clone <repo-url>
cd NetworkModuleTest

# ServerEngine 컴파일 검증
./scripts/verify_macos.sh

# 전체 서버 빌드
./scripts/build_unix.sh --config Release
```

---

## 기본 포트

| 실행 파일 | Windows 기본값 | Linux / macOS 기본값 |
|-----------|---------------|----------------------|
| TestDBServer | 18002 | 9001 |
| TestServer | 19010 | 9000 |
| TestClient | (접속 대상) | (접속 대상) |

> PowerShell 스크립트는 포트 충돌 시 자동으로 다음 빈 포트로 fallback합니다. 고정하려면 `-DisablePortFallback`을 사용하세요.

---

## 문서 진입점

| 문서 | 내용 |
|------|------|
| `Doc/README.md` | 전체 문서 인덱스 |
| `Doc/02_Architecture.md` | 아키텍처 |
| `Doc/03_Protocol.md` | 프로토콜 |
| `Doc/04_API.md` | API |
| `Doc/05_DevelopmentGuide.md` | **플랫폼별 빌드/실행/테스트 상세 가이드** |
| `Doc/06_SolutionGuide.md` | 솔루션 구조 |
| `Doc/07_VisualMap.md` | 코드-문서 매핑 |

`Doc/Architecture`, `Doc/Performance`, `Doc/Reports` 하위에는 이력/보고서 성격 문서가 포함되어 있습니다.
