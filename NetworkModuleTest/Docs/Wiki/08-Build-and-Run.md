# 08. 빌드 및 실행

## 개요

NetworkModuleTest는 Windows(MSBuild) 및 Linux(CMake/Docker) 빌드를 모두 지원한다.
실행 순서는 TestDBServer → TestServer → TestClient 순서를 지켜야 한다.

---

## 1. Windows 빌드 (MSBuild)

### 요구 사항

- Visual Studio 2022 Professional 이상
- Windows SDK (소켓/RIO API 지원)
- PowerShell 5.1 이상

### MSBuild 직접 실행

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe' `
    'NetworkModuleTest\NetworkModuleTest.sln' `
    /p:Configuration=Debug `
    /p:Platform=x64 `
    /m
```

| 옵션 | 값 | 설명 |
|------|----|------|
| `/p:Configuration` | `Debug` / `Release` | 빌드 구성 |
| `/p:Platform` | `x64` | 대상 플랫폼 (64비트) |
| `/m` | — | 병렬 빌드 활성화 |

출력 바이너리 위치: `x64\Debug\` (솔루션 루트 기준)

### 스크립트 빌드

전체 빌드:

```powershell
.\build_all.ps1 -Configuration Debug -Platform x64
```

개별 빌드 스크립트:

| 스크립트 | 설명 |
|----------|------|
| `build_all.ps1` | 솔루션 전체 빌드 |
| `build_serverengine.ps1` | ServerEngine 라이브러리만 빌드 |
| `build_testserver.ps1` | TestServer 프로젝트만 빌드 |
| `build_common.ps1` | 공통 빌드 설정 스크립트 |

---

## 2. 실행 순서

TestDBServer → TestServer → TestClient 순서로 기동해야 한다.
TestServer는 기동 시 TestDBServer에 연결을 시도하므로, TestDBServer가 먼저 준비되어 있어야 한다.

```
1. TestDBServer  (포트 8002)  — DB 이벤트 처리 서버
2. TestServer    (포트 9000)  — 클라이언트 연결 서버
3. TestClient                 — 연결 클라이언트
```

> **참고**: TestDBServer 기본 코드 포트는 8001이지만 스크립트 기본값은 8002를 사용한다. 포트 혼동 주의.

### 개별 실행 스크립트

| 스크립트 | 설명 |
|----------|------|
| `run_allServer.ps1` | TestDBServer + TestServer를 순서대로 기동 (권장) |
| `run_server.ps1` | TestServer만 기동 |
| `run_dbServer.ps1` | TestDBServer만 기동 |
| `run_client.ps1` | TestClient 실행 |

### run_allServer.ps1 사용 예시

```powershell
# 기본 실행 (포트 자동 할당)
.\run_allServer.ps1 -Configuration Debug -Platform x64

# 포트 고정 실행
.\run_allServer.ps1 -Configuration Debug -Platform x64 `
    -ServerPort 9000 -DbPort 8002 -DisablePortFallback

# 같은 창에서 실행
.\run_allServer.ps1 -Configuration Debug -Platform x64 -NoNewWindow
```

> `-DisablePortFallback` 없이 실행하면 기본 포트가 사용 중일 때 자동으로 다음 빈 포트로 이동한다.

### 클라이언트 연결

```powershell
.\run_client.ps1 -Configuration Debug -Platform x64 -ServerPort 9000
```

---

## 3. 테스트 실행

| 스크립트 | 설명 |
|----------|------|
| `run_test.ps1` | 단일 클라이언트 통합 테스트 |
| `run_test_2clients.ps1` | 2개 클라이언트 동시 연결 테스트 |
| `run_test_auto.ps1` | 자동화 테스트 (CI용) |
| `run_perf_test.ps1` | 성능 테스트 |

---

## 4. Linux / Docker 빌드

Linux 환경 테스트는 `test_linux/` 폴더에서 관리한다.

```
test_linux/
├── CMakeLists.txt
├── docker-compose.yml
├── Dockerfile
├── run_docker_test.ps1
└── scripts/
```

### Docker 테스트 실행

```powershell
.\test_linux\run_docker_test.ps1
```

또는 Docker Compose 직접 사용:

```bash
cd test_linux
docker-compose up --build
```

### CMake 빌드 (Linux 직접)

```bash
cd test_linux
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

> Linux 빌드 시 `libpq-dev`가 필요하다 (PostgreSQL 클라이언트 헤더). Dockerfile에 이미 포함되어 있다.

---

## 5. 자주 발생하는 문제

| 증상 | 원인 | 해결 |
|------|------|------|
| TestServer 시작 시 DB 연결 실패 | TestDBServer가 준비되지 않음 | TestDBServer를 먼저 기동 |
| `WSAECONNREFUSED` 로그 반복 | DB 서버 기동 중 | 1초 간격 자동 재연결, 기다리면 자동 복구 |
| 빌드 오류 `libpq not found` | PostgreSQL 헤더 누락 | `libpq-dev` 설치 또는 Docker 빌드 사용 |
| 포트 충돌 | 이전 프로세스가 포트 점유 중 | `kill_test_procs.ps1` 실행 후 재기동 |

```powershell
# 테스트 프로세스 강제 종료
.\kill_test_procs.ps1
```
