# Linux Docker 통합 테스트 실행 가이드

Windows 개발 환경에서 Docker를 통해 Linux(epoll / io_uring) 빌드 검증 및
3-tier 런타임 통합 테스트를 수행하고, 결과를 git에 푸시하는 방법을 설명합니다.

---

## 사전 요구 사항

| 항목 | 확인 명령 |
|------|-----------|
| Docker Desktop (Linux 컨테이너 모드) | `docker --version` |
| Docker Compose v2 | `docker compose version` |
| Git | `git --version` |

> Docker Desktop → Settings → General → **"Use the WSL 2 based engine"** 활성화 권장

---

## 폴더 구조

```
NetworkModuleTest/
├── Doc/Performance/Logs/          ← 테스트 결과가 여기 저장됨 (볼륨 마운트 대상)
│   └── 20260302_153000_linux/     ← 실행마다 타임스탬프 폴더 생성
│       ├── meta_epoll.txt         ← 파라미터, 커널, 소요시간, PASS/FAIL
│       ├── client_epoll.txt       ← TestClient 전체 출력
│       ├── meta_iouring.txt
│       └── client_iouring.txt
└── test_linux/
    ├── Dockerfile
    ├── docker-compose.yml
    ├── run_docker_test.ps1        ← Windows 호스트 런처 (모든 단계 자동화)
    └── scripts/
        ├── build.sh               ← cmake + ninja 빌드
        ├── entrypoint_client.sh   ← 테스트 실행 + 결과 저장
        └── run_integration.sh     ← 단일 컨테이너 빠른 검증
```

---

## 결과 저장 원리

```
Windows 호스트                         Docker 컨테이너 (Linux)
────────────────────────────────       ───────────────────────────────
Doc/Performance/Logs/    ←──── 볼륨 마운트 ────→  /workspace/Doc/Performance/Logs/
  20260302_153000_linux/              entrypoint_client.sh가 이 경로에 파일 기록
    meta_epoll.txt                    → 컨테이너 종료 후에도 호스트에 남아있음
    client_epoll.txt
```

컨테이너가 **종료되어도** 결과 파일은 Windows 호스트에 그대로 유지됩니다.

---

## 순차 실행 가이드

### Step 1 — NetworkModuleTest 루트로 이동

```powershell
cd C:\MyGithub\PublicStudy\NetworkModuleTest
```

### Step 2 — 이미지 빌드 + 테스트 실행 (결과 자동 저장)

```powershell
# epoll + io_uring 모두 실행 (권장)
.\test_linux\run_docker_test.ps1

# 백엔드 지정
.\test_linux\run_docker_test.ps1 -Backend epoll
.\test_linux\run_docker_test.ps1 -Backend iouring
```

완료되면 `Doc\Performance\Logs\<타임스탬프>_linux\` 폴더에 결과 파일이 생성됩니다.

### Step 3 — 결과 확인

```powershell
# 최신 결과 폴더 확인
ls Doc\Performance\Logs\ | Sort-Object Name | Select-Object -Last 3

# 결과 내용 확인
cat Doc\Performance\Logs\<폴더명>\meta_epoll.txt
cat Doc\Performance\Logs\<폴더명>\client_epoll.txt
```

### Step 4 — git 푸시 (자동)

```powershell
# 테스트 실행과 동시에 결과를 자동 커밋/푸시
.\test_linux\run_docker_test.ps1 -Push

# 이미지 재빌드 없이 (코드 변경 없을 때)
.\test_linux\run_docker_test.ps1 -NoBuild -Push
```

`-Push` 플래그가 하는 일:
1. 새로 생성된 로그 폴더만 `git add`
2. `perf: Linux Docker 테스트 결과 <타임스탬프> [epoll+iouring] PASS` 메시지로 커밋
3. `git push origin main`

---

## 시나리오별 명령어 정리

| 상황 | 명령어 |
|------|--------|
| 처음 실행 (코드 변경 있음) | `.\test_linux\run_docker_test.ps1 -Push` |
| 코드 변경 없이 재테스트 | `.\test_linux\run_docker_test.ps1 -NoBuild -Push` |
| epoll만 빠르게 확인 | `.\test_linux\run_docker_test.ps1 -Backend epoll` |
| 단일 컨테이너 빠른 smoke test | `.\test_linux\run_docker_test.ps1 -Single` |
| 수동으로 결과만 푸시 | 아래 참조 |

### 수동 결과 푸시

```powershell
cd C:\MyGithub\PublicStudy
git add NetworkModuleTest/Doc/Performance/Logs/<폴더명>/
git commit -m "perf: Linux 테스트 결과 <폴더명>"
git push origin main
```

---

## 결과 파일 설명

### `meta_<backend>.txt`

```
date:        2026-03-02 15:30:01
backend:     epoll
server:      server:9000
num_clients: 10
num_pings:   5
kernel:      5.15.0-76-generic
wait_sec:    3
duration_sec: 12
result:      PASS
```

### `client_<backend>.txt`

TestClient의 전체 stdout/stderr 출력 (접속 성공/실패, 레이턴시 통계 등)

---

## 검증된 테스트 결과

| 날짜 | 로그 폴더 | 백엔드 | 결과 | 비고 |
|------|----------|--------|------|------|
| 2026-03-02 | `20260302_183433_linux` | epoll + io_uring | **PASS** | AsyncScope 버그 수정 후 첫 PASS |
| 2026-03-02 | `20260302_191739_linux` | epoll + io_uring | **PASS** | 최종 확인 (exit code 수정 포함) |

### 과거 실패 이력
| 날짜 | 로그 폴더 | 증상 | 원인 |
|------|----------|------|------|
| 2026-03-02 | `20260302_180810_linux` | io_uring FAIL (EAGAIN) | `AsyncScope::mCancelled` 미초기화 |
| 2026-03-02 | `20260302_182729_linux` | io_uring FAIL (EAGAIN) | 동일 (빌드 전 이미지 사용) |

**수정 내용 (2026-03-02)**:
- `AsyncScope::Reset()` 메서드 추가 (`Server/ServerEngine/Concurrency/AsyncScope.h`)
- `Session::Reset()`에서 `mAsyncScope.Reset()` 호출 (`Server/ServerEngine/Network/Core/Session.cpp`)
- 이유: 세션 풀 재사용 시 `mCancelled=true`가 잔존하여 모든 로직 태스크 silently skip

---

## 문제 해결

| 증상 | 원인 | 해결 |
|------|------|------|
| `volume mount denied` | Docker Desktop 파일 공유 미설정 | Settings → Resources → File Sharing → `C:\MyGithub` 추가 |
| `liburing not found` 빌드 실패 | Dockerfile 캐시 오래됨 | `docker-compose build --no-cache` |
| 결과 파일이 빈 파일 | 서버 start 실패 | `client_<backend>.txt` 내용 확인, server 컨테이너 로그 확인 |
| `io_uring` FAIL, `epoll` PASS | 커널 5.1+ 미만 또는 세션 풀 재사용 버그 | `AsyncScope::Reset()` 수정 적용 여부 확인; Docker Desktop 커널 버전 확인 (`uname -r`) |

---

## Docker 컨테이너 수동 조작

```powershell
# 컨테이너 내부 bash 접속 (디버깅용)
docker-compose -f test_linux/docker-compose.yml run --rm --entrypoint bash client_epoll

# 서버 로그 확인
docker-compose -f test_linux/docker-compose.yml logs server

# 컨테이너/네트워크 정리
docker-compose -f test_linux/docker-compose.yml down --remove-orphans
```
