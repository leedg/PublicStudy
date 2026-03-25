# NetworkModuleTest 퍼포먼스 테스트 가이드라인

**버전**: 1.0
**작성일**: 2026-02-28
**적용 대상**: TestServer / TestDBServer / TestClient (Windows x64)

---

## 목차

1. [목적 및 원칙](#1-목적-및-원칙)
2. [테스트 환경 고정 규칙](#2-테스트-환경-고정-규칙)
3. [테스트 종류 및 시나리오 정의](#3-테스트-종류-및-시나리오-정의)
4. [실행 절차 (표준 워크플로)](#4-실행-절차-표준-워크플로)
5. [측정 지표 및 수집 방법](#5-측정-지표-및-수집-방법)
6. [합격 기준 (Pass/Fail)](#6-합격-기준-passfail)
7. [결과 기록 규칙 (누적 히스토리)](#7-결과-기록-규칙-누적-히스토리)
8. [결과 해석 가이드](#8-결과-해석-가이드)
9. [트러블슈팅 체크리스트](#9-트러블슈팅-체크리스트)
10. [이력 및 기준선 갱신 규칙](#10-이력-및-기준선-갱신-규칙)

---

## 1. 목적 및 원칙

### 목적

- 코드 변경 전후 성능 **회귀(regression)** 여부를 객관적으로 판단
- 동시 접속 한계, RTT, 리소스 소비를 **수치로 추적**
- 안정성(Graceful Shutdown, WAL 복구, 재연결)과 성능을 **같은 세션에서 함께 검증**

### 핵심 원칙

| 원칙 | 설명 |
|------|------|
| **Release 빌드 기준** | 모든 수치는 x64 Release 기준. Debug는 탐색/재현용으로만 사용 |
| **동일 환경 반복** | 같은 머신, 같은 네트워크, 같은 빌드 해시에서만 비교 |
| **최소 3회 반복** | 중앙값(median) 기준으로 판정. 단발 수치로 판단 금지 |
| **누적 기록 필수** | 매 실행 결과를 `PERF_HISTORY.md`에 자동 누적 |
| **PC 재부팅 후 필수** | PC 강제 종료 또는 재부팅 직후에는 Phase 0 → Phase 1 순서로 진행 |

---

## 2. 테스트 환경 고정 규칙

### 실행 전 체크리스트

```powershell
# 1. 잔류 프로세스 정리 (매 세션 시작 필수)
.\kill_test_procs.ps1

# 2. 전원 옵션 고성능 설정 확인
powercfg /getactivescheme
# → GUID 8c5e7fda... (고성능) 이어야 함
# 아닌 경우: powercfg /setactive 8c5e7fda-e8bf-45a6-a6cc-4b3c619a6a61

# 3. 백그라운드 고부하 프로세스 없는지 확인
Get-Process | Sort-Object CPU -Descending | Select-Object -First 10
```

### 빌드 기준

| 구분 | 설정 | 용도 |
|------|------|------|
| **퍼포먼스 측정** | x64 **Release** | 공식 수치 기준 |
| 버그 재현 / 탐색 | x64 Debug | 수치 비교 불가 |

```powershell
# Release 빌드
msbuild NetworkModuleTest.sln /p:Configuration=Release /p:Platform=x64 /m
```

### 프로세스 기동 순서 (불변)

```
TestDBServer (8002) → TestServer (9000) → TestClient(s)
```

각 프로세스 기동 후 **1200ms** 대기 (연결 초기화 완료 보장).

---

## 3. 테스트 종류 및 시나리오 정의

### 3.1 안정성 테스트 (Stability)

| ID | 이름 | 내용 | 성공 기준 |
|----|------|------|-----------|
| S-1 | Graceful Shutdown | 2 클라이언트, 30초 실행 후 Named Event로 종료 | ExitCode=0, DBTaskQueue 드레인 완료 |
| S-2 | Forced Shutdown + WAL | 서버 강제 kill 후 재기동 시 WAL 상태 확인 | 재기동 로그에 WAL 메시지 존재 |
| S-3 | 클라이언트 재연결 | 서버 재기동 중 `WSAECONNREFUSED(10061)` → 1s 재시도, 기타 → 지수 백오프 | 서버 재기동 후 클라이언트 자동 재연결 성공 |

### 3.2 퍼포먼스 테스트 (Performance)

| ID | 이름 | 클라이언트 수 | 지속 시간 | 측정 항목 |
|----|------|--------------|-----------|-----------|
| P-0 | Smoke | 1 | 10s | RTT, ExitCode |
| P-1 | Baseline | 1 | 60s | RTT min/avg/max, CPU%, WS, Handles |
| P-2 | Ramp-up | 10→100→500→1000 | 30s/단계 | 연결 수, 오류율, WS, Handles |
| P-3 | Sustain | 500 (안정 한계) | 30분 | 5분마다 WS/Handles 추이 (누수 확인) |
| P-4 | Burst | 500 → 순간 1000 → 500 | 10s + 60s | 복구 시간(`T_recover`) |
| P-5 | Churn | 100 clients × 5회 반복 연결/해제 | 30s × 5 | Handles 누적 여부 |

### 3.3 백엔드 비교 테스트 (Backend Comparison)

현재 서버는 `auto` 모드로 RIO를 자동 선택.
IOCP 강제 모드를 지원하려면 `--backend iocp` 옵션 추가 필요 (미구현).

| 조건 | 현재 상태 |
|------|-----------|
| RIO (auto) | 측정 가능 |
| IOCP 강제 | **코드 추가 필요** (`main.cpp` → `NetworkEngineFactory` 파라미터 전달) |

**IOCP 강제 옵션 추가 후 비교 대상:**

| 클라이언트 수 | RIO RTT | IOCP RTT | RIO WS | IOCP WS |
|---|---|---|---|---|
| 100 | - | - | - | - |
| 500 | - | - | - | - |
| 1000 | WSA 10055 재현 확인 | 안정 여부 | - | - |

---

## 4. 실행 절차 (표준 워크플로)

### 표준 실행 (run_perf_test.ps1 사용)

```powershell
# 전체 Phase 실행 (권장)
.\run_perf_test.ps1 -Phase all -RampClients 10,100,500,1000 -SustainSec 30 -BinMode Release

# Phase 별 개별 실행
.\run_perf_test.ps1 -Phase 0   # Smoke Test만
.\run_perf_test.ps1 -Phase 1   # 안정성 테스트만
.\run_perf_test.ps1 -Phase 2   # Ramp-up만

# 빠른 검증 (CI용)
.\run_perf_test.ps1 -Phase 0 -SkipSmoke:$false -SustainSec 10
```

### PC 재부팅 후 복구 절차

```
1. kill_test_procs.ps1 실행 (잔류 정리)
2. Phase 0 Smoke Test (Release 정상 기동 확인)
3. Phase 1-B Forced Shutdown + WAL 복구 확인
4. 이상 없으면 Phase 2 진행
```

### 수동 장시간 Sustain 테스트 (P-3)

```powershell
$binDir = 'C:\MyGithub\PublicStudy\NetworkModuleTest\x64\Release'
# DBServer, TestServer 기동 후 500 클라이언트 기동
# 5분마다 아래 실행
while ($true) {
    Get-Process TestServer,TestDBServer -ErrorAction SilentlyContinue |
        Select-Object Name,
            @{N='WS_MB';E={[math]::Round($_.WorkingSet64/1MB,1)}},
            HandleCount,
            @{N='Threads';E={$_.Threads.Count}} |
        Format-Table -AutoSize
    Start-Sleep -Seconds 300
}
```

---

## 5. 측정 지표 및 수집 방법

### 핵심 지표

| 지표 | 수집 위치 | 비고 |
|------|-----------|------|
| RTT min/avg/max | TestClient 표준 출력 (`Min RTT:`, `Avg RTT:`, `Max RTT:`) | ms 단위 |
| 서버 Working Set | `Get-Process TestServer \| .WorkingSet64` | 측정 시점 스냅샷 |
| 핸들 수 | `Get-Process TestServer \| .HandleCount` | 누수 판단 기준 |
| 스레드 수 | `Get-Process TestServer \| .Threads.Count` | 이상 증가 감지 |
| 연결 성공 수 | 서버 로그 `Client connected - IP:` 라인 수 | 목표 대비 비율 |
| 오류/경고 수 | 서버 로그 `[ERROR]`, `[WARN]` 라인 수 | 허용 임계치 설정 |
| ExitCode | `$proc.ExitCode` | 0이어야 정상 |
| DBTaskQueue 드레인 | 서버 로그 `DBTaskQueue shutdown complete` | Graceful 판정 기준 |

### 클라이언트 RTT 수집 예시 (로그 파싱)

```powershell
function Parse-RTT([string]$LogFile) {
    $lines = Get-Content $LogFile
    $avg = ($lines | Select-String "Avg RTT:" | Select-Object -Last 1) -replace ".*Avg RTT:\s*", "" -replace "ms.*", ""
    $max = ($lines | Select-String "Max RTT:" | Select-Object -Last 1) -replace ".*Max RTT:\s*", "" -replace "ms.*", ""
    return "avg=${avg}ms max=${max}ms"
}
```

---

## 6. 합격 기준 (Pass/Fail)

### Smoke / Baseline

| 항목 | Pass 조건 |
|------|-----------|
| 연결 수 | 목표 100% |
| 오류/경고 | 0 |
| ExitCode | Server=0, Client=0 |
| RTT avg | < 10ms (loopback, Release 기준) |

### Ramp-up

| 단계 | Pass 조건 |
|------|-----------|
| 10, 100 | 연결 100%, 오류 0 |
| 500 | 연결 ≥ 95%, 오류 < 10 |
| 1000 | 연결 ≥ 90%, `WSA 10055` 없음 (현재 미달 — 개선 목표) |

### Sustain (30분)

| 항목 | Pass 조건 |
|------|-----------|
| Working Set 증가 | < 5% (30분간) |
| 핸들 수 | 안정 (선형 증가 없음) |
| 오류 발생 | 없음 |

### Graceful Shutdown

| 항목 | Pass 조건 |
|------|-----------|
| DBTaskQueue 드레인 | `shutdown complete` 로그 존재 |
| 모든 세션 종료 | `All sessions closed` 로그 존재 |
| ExitCode | 0 |

---

## 7. 결과 기록 규칙 (누적 히스토리)

### 파일 구조

```
Doc/Performance/
├── PerformanceTestGuideline.md   ← 이 문서
├── Logs/
│   ├── PERF_HISTORY.md           ← 누적 결과 (자동 갱신)
│   ├── 20260226_192852_baseline.md
│   ├── 20260227_111258_rampup.md
│   └── <RunTag>/                 ← run_perf_test.ps1 실행별 상세 로그
│       ├── p0_srv.txt
│       ├── p1a_srv.txt
│       ├── p2_n100_srv.txt
│       └── ...
└── Benchmarking.md
```

### PERF_HISTORY.md 구조 (자동 누적)

```markdown
# NetworkModuleTest 퍼포먼스 테스트 누적 기록

---

## 실행: 20260226_192852
- 빌드: x64 Debug
...

---

## 실행: 20260228_HHMMSS
- 빌드: x64 Release
...
```

### 규칙

- `run_perf_test.ps1`이 실행될 때마다 `PERF_HISTORY.md`에 **섹션을 추가(append)**
- 이전 기록은 절대 수정하지 않음
- 수동 테스트 결과도 동일 포맷으로 수동 추가
- 파일명: `YYYYMMDD_HHMM_<시나리오명>_<빌드모드>.md`

---

## 8. 결과 해석 가이드

### 현재 수립된 기준선 (2026-02-28 기준)

| 조건 | 최대 안정 동접 | 비고 |
|------|---------------|------|
| x64 Debug, RIO, 동일 머신 | **500** | Ramp-up 1차 (02-27) |
| x64 Release, RIO, 동일 머신 | 측정 중 | 이번 세션 목표 |

### WSA 10055 해석

RIO 1000 클라이언트 시 `Failed to create RIO request queue (WSA: 10055)` 발생.

| 원인 후보 | 확인 방법 |
|-----------|-----------|
| RIO 등록 버퍼 총량 한계 | `RIORegisterBuffer` 반환값 / 총 할당 크기 확인 |
| 소켓당 RIO queue 크기 과다 | `RIOCreateRequestQueue` SQ/CQ depth 파라미터 확인 |
| 시스템 Locked Memory 한계 | `SeLockMemoryPrivilege` 권한 및 `AWE` 설정 확인 |
| 동일 머신 리소스 경합 | 클라이언트/서버 분리 머신에서 재측정 |

### 성능 회귀 판단 기준

이전 실행 대비:

| 변화 | 판단 |
|------|------|
| RTT avg +10% 이내 | 무시 (노이즈 범위) |
| RTT avg +10~30% | 주의 — 원인 파악 |
| RTT avg +30% 이상 | **회귀 — 커밋 차단** |
| 최대 안정 동접 감소 | **회귀 — 커밋 차단** |
| WS 30분 후 선형 증가 | **누수 의심 — 조사 필요** |

---

## 9. 트러블슈팅 체크리스트

### 서버가 기동 직후 종료될 때

```
□ 포트 충돌 확인: netstat -ano | findstr :9000
□ 이전 프로세스 잔존: .\kill_test_procs.ps1
□ 바이너리 빌드 최신 여부: msbuild /p:Configuration=Release
```

### 클라이언트가 연결 못할 때

```
□ 기동 순서 확인: DBServer → TestServer → Client (각 1.2s 간격)
□ 서버 로그에 "BaseNetworkEngine started" 있는지 확인
□ 방화벽: Windows Defender Firewall → 인바운드 9000 허용
```

### RTT가 비정상적으로 높을 때 (loopback인데 >10ms)

```
□ 전원 옵션 고성능 확인
□ 백그라운드 고부하 프로세스 확인
□ Debug 빌드로 측정한 것 아닌지 확인
□ 동일 머신에 서버+클라이언트 과다 동시 실행 여부
```

### WSA 10055 (RIO resource limit)

```
□ 클라이언트 수를 500으로 낮춰서 재확인
□ 서버 재기동 후 재시도 (이전 세션 리소스 정리)
□ TestServer.exe를 관리자 권한으로 실행 (SeLockMemoryPrivilege)
□ RIO queue depth 설정값 확인 (소켓당 크기 줄이기 검토)
```

---

## 10. 이력 및 기준선 갱신 규칙

### 기준선 갱신 조건

기준선(`Baseline`)은 다음 조건이 모두 충족될 때만 갱신:

1. **Release 빌드**에서 3회 이상 반복 측정
2. 이전 기준선 대비 **유의미한 개선** (RTT -10% 이상, 또는 동접 +10% 이상)
3. Sustain 30분 테스트 **Pass**
4. 담당자가 `PERF_HISTORY.md`에 `[BASELINE UPDATE]` 태그로 명시

### 코드 머지 전 성능 체크 규칙

- `feat/*`, `refactor/*` 브랜치는 머지 전 최소 **Phase 0 + Phase 2 Ramp-up** 실행
- 결과를 PR 설명에 첨부 (PERF_HISTORY.md 해당 섹션 링크)
- RTT 또는 최대 동접 **회귀 시 머지 차단**

### 장기 추이 관리

`PERF_HISTORY.md`에서 Ramp-up Pass/Fail 추이를 주기적으로 검토:

```
날짜       | 1000 동접 | RTT avg (1 cli) | WS (500 cli)
---------- | --------- | --------------- | ------------
2026-02-27 | FAIL      | -               | 44.9 MB
2026-02-28 | ?         | ?               | ?
```

---

**다음 액션 (현재 미완료 항목)**

1. `--backend iocp` 옵션 추가 → IOCP vs RIO 비교 측정
2. RIO 1000+ `WSA 10055` 원인 분석 및 queue depth 튜닝
3. Sustain 30분 테스트 (P-3) 실행
4. 클라이언트-서버 분리 머신 테스트 환경 구성 (로컬 리소스 경합 제거)
