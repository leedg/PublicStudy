# 개발 가이드 (상세)

---

## 플랫폼별 빠른 진입

| 플랫폼 | 빌드 방법 | 실행/테스트 방법 |
|--------|-----------|-----------------|
| **Windows** | `.\build_all.ps1` (MSBuild 자동 탐지) | `.\run_allServer.ps1` → `.\run_client.ps1` |
| **Linux** | `scripts/build_unix.sh` 또는 Docker | `test_linux/run_docker_test.ps1` (Windows 호스트에서) |
| **macOS** | `scripts/build_unix.sh` | `./build/TestServer`, `./build/TestClient` |

---

## 1. Windows 환경

### 1-1. 선결 조건

| 항목 | 버전/비고 |
|------|-----------|
| Visual Studio 2022 | MSVC v143 + "C++ 빌드 도구" + "Windows SDK 10.0" 구성 요소 필요 |
| 또는 VS 2022 Build Tools | GUI 없이 빌드만 필요한 경우: [Build Tools 다운로드](https://visualstudio.microsoft.com/downloads/) → "Tools for Visual Studio" |
| PowerShell | 5.1 이상 (Windows 기본 포함) |
| Git | 클론에 필요 |

> MSBuild는 PATH 등록이 없어도 자동 탐지합니다 (vswhere → VS2022 설치 경로 순).

### 1-2. 빌드

```powershell
# 클론 후 루트 디렉터리에서 실행
git clone <repo-url>
cd NetworkModuleTest

.\build_all.ps1                   # Release x64 (기본값)
.\build_all.ps1 -Configuration Debug
```

또는 스크립트 디렉터리에서:

```powershell
.\scripts\build_windows.ps1 -Configuration Release
.\scripts\build_windows.ps1 -Rebuild   # 클린 후 재빌드
```

또는 Visual Studio에서 `NetworkModuleTest.sln`을 열고 x64 Debug/Release 선택 후 빌드.
권장 빌드 순서: `ServerEngine` → `TestDBServer` → `TestServer` → `TestClient`

### 1-3. 실행

```powershell
# 서버 + 클라이언트 한 번에 (포트 충돌 시 자동 fallback)
.\run_allServer.ps1
.\run_client.ps1

# 개별 실행 (기본 포트: DBServer=18002, Server=19010)
.\run_dbServer.ps1
.\run_server.ps1
.\run_client.ps1

# 수동 실행 (포트 고정)
.\Server\TestDBServer\x64\Release\TestDBServer.exe -p 18002
.\Server\TestServer\x64\Release\TestServer.exe -p 19010 --db-host 127.0.0.1 --db-port 18002
.\Client\TestClient\x64\Release\TestClient.exe --host 127.0.0.1 --port 19010
```

> `-DisablePortFallback` 플래그로 포트 자동 fallback을 끌 수 있습니다.

### 1-4. 테스트 (Windows)

```powershell
# 통합 테스트 자동 실행 (5초)
.\run_test_auto.ps1 -RunSeconds 5

# 구조 동기화 검증 생략
.\run_test_auto.ps1 -RunSeconds 5 -SkipStructureSyncCheck

# AsyncIO 백엔드 단위 테스트 (GTest 불필요)
.\build_all.ps1 -Configuration Debug
.\Server\Tests\IOCPTest\x64\Debug\IOCPTest.exe
.\Server\Tests\RIOTest\x64\Debug\RIOTest.exe
# 성공 기준: "Result: N passed, 0 failed"
```

---

## 2. Linux 환경

### 2-1. 방법 A — Docker (Windows 호스트에서 Linux 테스트)

Windows PC에서 Linux 통합 테스트를 돌리는 방법입니다. Docker Desktop만 있으면 됩니다.

**선결 조건:**
- Docker Desktop for Windows (WSL2 백엔드 권장)
- [다운로드](https://www.docker.com/products/docker-desktop)

```powershell
# epoll + io_uring 두 백엔드 모두 테스트 (이미지 빌드 포함)
.\test_linux\run_docker_test.ps1

# epoll 만
.\test_linux\run_docker_test.ps1 -Backend epoll

# 이미지 재빌드 없이 재실행
.\test_linux\run_docker_test.ps1 -NoBuild

# 결과를 git에 자동 커밋/푸시
.\test_linux\run_docker_test.ps1 -Push
```

> Docker가 미설치되거나 데몬이 꺼져 있으면 스크립트가 시작 전에 오류 메시지를 출력합니다.

### 2-2. 방법 B — 네이티브 Linux 빌드

실제 Linux 머신 또는 WSL2 터미널에서 직접 빌드하는 방법입니다.

**선결 조건 (Ubuntu 22.04 기준):**

```bash
sudo apt-get update
sudo apt-get install -y cmake build-essential pkg-config

# io_uring 백엔드 사용 시 (선택)
sudo apt-get install -y liburing-dev

# SQLite DB 지원 사용 시 (선택)
sudo apt-get install -y libsqlite3-dev
```

**빌드:**

```bash
# 기본 빌드 (epoll 백엔드)
./scripts/build_unix.sh

# io_uring + SQLite DB 지원 포함
./scripts/build_unix.sh --enable-io-uring --enable-db

# Release 빌드 / 클린 빌드
./scripts/build_unix.sh --config Release
./scripts/build_unix.sh --clean --config Release

# 결과 위치: ./build/
```

**실행 (Linux 기본 포트: DBServer=9001, Server=9000):**

```bash
./build/DBServer -p 9001 -l INFO &
./build/TestServer -p 9000 --db-host 127.0.0.1 --db-port 9001 -l INFO &
./build/TestClient --host 127.0.0.1 --port 9000 --pings 5
```

---

## 3. macOS 환경

macOS는 io_uring 미지원, IOCP 미지원입니다. epoll 기반 kqueue 백엔드로 ServerEngine 컴파일 검증에 초점을 맞춥니다.

### 3-1. 선결 조건

```bash
# Xcode Command Line Tools (컴파일러)
xcode-select --install

# Homebrew로 cmake 설치
brew install cmake pkg-config
```

### 3-2. 빌드

```bash
# ServerEngine 검증용 빌드 (기본)
./scripts/verify_macos.sh

# Release 빌드 / 클린 빌드
./scripts/verify_macos.sh --config Release
./scripts/verify_macos.sh --clean

# 전체 서버 빌드 (TestServer, DBServer 포함)
./scripts/build_unix.sh --config Release

# 결과 위치: ./build-mac/ (verify_macos) 또는 ./build/ (build_unix)
```

### 3-3. 실행

```bash
# 빌드 후 실행 (기본 포트: DBServer=9001, Server=9000)
./build/DBServer -p 9001 &
./build/TestServer -p 9000 --db-host 127.0.0.1 --db-port 9001 &
./build/TestClient --host 127.0.0.1 --port 9000 --pings 5
```

> 전체 통합 테스트가 필요하면 Docker Desktop for Mac + `test_linux/run_docker_test.ps1` 또는 Linux 네이티브 환경을 이용하세요.

---

## 4. 빌드 시스템 정리

| 시스템 | 대상 플랫폼 | 진입 스크립트 |
|--------|-------------|--------------|
| MSBuild (.sln) | Windows 전용 | `build_all.ps1`, `scripts/build_windows.ps1` |
| CMake | Linux / macOS | `scripts/build_unix.sh`, `scripts/verify_macos.sh` |
| Docker + CMake | Linux 테스트 (Windows 호스트) | `test_linux/run_docker_test.ps1` |

> 루트 `CMakeLists.txt`는 CMake 빌드의 진입점입니다. `BUILD_SERVER_ENGINE`, `BUILD_TEST_SERVER`, `BUILD_DB_SERVER` 플래그로 빌드 대상을 선택합니다. `build_unix.sh`가 이 플래그를 자동으로 설정합니다.

---

## 5. DB 지원

### 5-1. Windows

`ENABLE_DATABASE_SUPPORT` 전처리기 정의를 TestServer 프로젝트에 추가하면 SQLite / ODBC / OLE DB 백엔드가 활성화됩니다. (프로젝트 속성 → C/C++ → 전처리기)

### 5-2. Linux / macOS

`--enable-db` 플래그를 `build_unix.sh`에 전달합니다. SQLite 헤더(`libsqlite3-dev`)가 필요합니다.

---

## 6. 로그 / 디버깅

```bash
# 로그 레벨: DEBUG / INFO / WARN / ERROR
TestServer.exe -l DEBUG
./build/TestServer -l DEBUG
```

---

## 7. 통합 테스트 (Windows 자동화)

```powershell
# 기본 통합 테스트
.\run_test_auto.ps1 -RunSeconds 5

# 구조 동기화 검증 생략
.\run_test_auto.ps1 -RunSeconds 5 -SkipStructureSyncCheck

# 구조 동기화 검증만 단독 실행
.\ModuleTest\ServerStructureSync\validate_server_structure_sync.ps1
```

---

## 8. DB 모듈 테스트

### 자동 테스트 (Docker 기반, `scripts/db_tests/`)

각 스크립트가 ODBC 드라이버 설치 → Docker 컨테이너 기동 → 빌드 → 실행 → 정리를 자동으로 수행합니다.

```powershell
.\scripts\db_tests\run_sqlite_test.ps1      # SQLite (드라이버 불필요, 즉시 실행)
.\scripts\db_tests\run_mssql_test.ps1       # MSSQL ODBC (Docker SQL Server)
.\scripts\db_tests\run_postgres_test.ps1    # PostgreSQL ODBC (Docker Postgres)
.\scripts\db_tests\run_mysql_test.ps1       # MySQL ODBC (Docker MySQL)
.\scripts\db_tests\run_oledb_test.ps1       # OLE DB (Docker SQL Server 재사용)
```

테스트 케이스 (`scripts/db_tests/src/db_functional_test.cpp`):

| 테스트 | 내용 | SQLite | ODBC / OLE DB |
|--------|------|:------:|:-------------:|
| T01 | 타입 라운드트립 (string/int/long long/double/bool/null) | ✓ | ✓ |
| T02 | `IsNull()` 후 `GetString()` 동일 컬럼 (컬럼 캐시 검증) | ✓ | ✓ |
| T03 | `FindColumn` 대소문자 무관 | ✓ | ✓ |
| T04 | `Get*()` before `Next()` — 안전한 기본값 반환 | ✓ | — |
| T05 | `IConnection::BeginTransaction` / `Rollback` | ✓ | ✓ |
| T06 | `IDatabase::BeginTransaction` throws (설계 검증) | — | ✓ |

### 수동 테스트 (DBModuleTest, Docker 불필요)

```powershell
cd ModuleTest/DBModuleTest

# SQLite 즉시 실행
.\scripts\run-db-tests.ps1 -Backend sqlite -Build

# MSSQL ODBC (로컬 SQL Server 필요)
$env:DB_MSSQL_ODBC = "Driver={ODBC Driver 17 for SQL Server};Server=localhost,1433;Database=db_func_test;UID=sa;PWD=Test1234!;TrustServerCertificate=yes;"
.\scripts\run-db-tests.ps1 -Backend mssql

# OLE DB (MSOLEDBSQL 또는 SQLOLEDB, 로컬 SQL Server 필요)
$env:DB_OLEDB = "Provider=MSOLEDBSQL;Server=localhost,1433;Database=db_func_test;UID=sa;PWD=Test1234!;Encrypt=Optional;"
.\scripts\run-db-tests.ps1 -Backend oledb

# 전체 백엔드 순차 실행
.\scripts\run-db-tests.ps1 -Backend all -Build
```

자세한 내용은 `ModuleTest/DBModuleTest/Docs/README.md` 참조.
