# PlatformPerfPlan — 멀티 플랫폼 성능 테스트 실행 계획

## 1. 개요

Windows(RIO/IOCP), Linux(epoll/io_uring), macOS(kqueue) 세 플랫폼 간 네트워크 엔진 성능을 비교·검증하는 테스트 자동화 계획.

## 2. 구성 요소

| 구성 요소 | 경로 | 역할 |
|-----------|------|------|
| `run_platform_perf.ps1` | `.\ ` | Windows + Docker(Linux) 통합 실행 진입점 |
| `run_perf_test.ps1` | `.\ ` | Windows 단독 성능 측정 |
| `run_docker_test.ps1` | `.\test_linux\ ` | Docker 컨테이너로 Linux 성능 측정 |
| `entrypoint_client.sh` | `.\test_linux\scripts\ ` | Docker 내부 클라이언트 부하 생성 |
| `run_native_perf.sh` | `.\test_linux\scripts\ ` | Linux 네이티브 성능 측정 |
| `update_perf_history.py` | `.\Doc\Performance\scripts\ ` | 측정 결과 이력 누적·변화량 계산 |
| `PERF_HISTORY.md` | `.\Doc\Performance\Logs\ ` | 성능 이력 데이터 |

## 3. 실행 방법

### 실행 시작점 (Windows 개발 기준)

```powershell
# 1. 통합 실행 (Windows + Docker Linux 동시 측정)
.\run_platform_perf.ps1 -ClientCounts 10 100

# 2. Docker(Linux)만 실행
.\test_linux\run_docker_test.ps1 -ClientCounts 10 100

# 3. 성능 이력/변화량만 갱신 (측정 로그가 이미 있을 때)
python .\Doc\Performance\scripts\update_perf_history.py --logs-root .\Doc\Performance\Logs
```

### 검증 항목

- [x] PowerShell 스크립트 파싱 OK (`run_perf_test.ps1`, `run_docker_test.ps1`, `run_platform_perf.ps1`)
- [x] Bash 문법 검사 OK (`entrypoint_client.sh`, `run_native_perf.sh`)
- [x] Python 문법 검사 OK (`update_perf_history.py`)

### 클라이언트 부하 방식

현재 구현은 **멀티 프로세스** 방식 기준입니다.
멀티 세션 단일 프로세스 방식은 선택 사항으로 남겨두었으며, 추후 별도 구현 예정입니다.
