# NetworkModuleTest 퍼포먼스 테스트 누적 기록

> 이 파일은 run_perf_test.ps1 실행 시 자동 갱신됩니다.
> 각 실행 결과는 섹션으로 누적됩니다. 이전 기록은 절대 수정하지 않습니다.

---

## 실행: 20260226_192852 (수동 기록)

- **빌드**: x64 Debug
- **시각**: 2026-02-26 19:28:52
- **시나리오**: Baseline (1 client, 10s)
- **서버 백엔드**: RIO (auto)

### 결과 요약

| 항목 | 값 |
|------|-----|
| 시나리오 | Baseline |
| 목표 연결 | 1 |
| 실제 연결 | 1 |
| 오류/경고 | 0 |
| Ping sent | 2 |
| Pong recv | 2 |
| RTT min/avg/max | 0ms / 2ms / 5ms |
| Graceful Shutdown | PASS |
| 판정 | **PASS** |

---

## 실행: 20260227_111258 (수동 기록)

- **빌드**: x64 Debug
- **시각**: 2026-02-27 11:12:58
- **시나리오**: Ramp-up (10 / 50 / 100 / 500 / 1000 clients)
- **서버 백엔드**: RIO (auto)

### Ramp-up 결과

| 단계 | 목표 연결 | 실제 연결 | 오류/경고 | Server WS(MB) | Server Handles | 판정 |
|------|-----------|-----------|-----------|---------------|----------------|------|
| 10   | 10        | 10        | 0         | 35.1          | 172            | **PASS** |
| 50   | 50        | 50        | 0         | 35.9          | 216            | **PASS** |
| 100  | 100       | 100       | 0         | 37.0          | 267            | **PASS** |
| 500  | 500       | 500       | 0         | 44.9          | 662            | **PASS** |
| 1000 | 1000      | 655       | 4425      | N/A           | N/A            | **FAIL** (WSA 10055) |

### 주요 관찰

- 500까지 안정 동작 (x64 Debug 기준 현재 한계선)
- 1000에서 `Failed to create RIO request queue (WSA: 10055)` 발생
- 재연결 폭주로 `Session initialized` 5081회 급증

---

## 실행: 20260228_124350 (run_perf_test.ps1 — 최종 확정)

- **빌드**: x64 **Release**
- **시각**: 2026-02-28 12:43:50
- **Phase**: all / **Ramp**: 10, 100, 500, 1000 / **각 단계 유지**: 30s
- **서버 백엔드**: RIO (auto)
- **실행 스크립트**: `.\run_perf_test.ps1 -Phase all -RampClients 10,100,500,1000 -SustainSec 30 -BinMode Release`

### Phase 0 — Smoke Test (Release, 1 client, 10s)

| 항목 | 값 |
|------|-----|
| 결과 | **PASS** |
| 연결 수 | 1 |
| RTT min/avg/max | 7ms / 7ms / 7ms, Pong=1 |
| Server WS / Handles / Threads | 22MB / 137 / 28 |
| DB WS / Handles / Threads | 22MB / 132 / 29 |
| [ERROR] 수 | 0 |
| 서버/클라이언트 정상 종료 | Yes / Yes |

### Phase 1 — 안정성 테스트

#### 1-A: Graceful Shutdown (2 clients, 30s)

| 항목 | 값 |
|------|-----|
| 결과 | **PASS** |
| 연결된 클라이언트 수 | 2 |
| 서버 리소스 (종료 직전) | WS=21.9MB, Handles=138, Threads=26 |
| DBTaskQueue 드레인 | Yes |
| [ERROR] 수 | 0 |

#### 1-B: Forced Shutdown + WAL Recovery

| 항목 | 값 |
|------|-----|
| WAL 상태 (재기동 후) | Clean (직전 Graceful이 완벽히 처리됨) |
| 클라이언트 자동 재연결 시도 | Yes |

> WAL=Clean: 직전 Graceful Shutdown이 모든 DB task를 flush 했음을 의미.
> PC 강제 종료 후 재기동 시에는 `WAL: Recovering N pending tasks` 가 나타날 것으로 예상.

### Phase 2 — 퍼포먼스 Ramp-up (x64 Release, RIO auto, 30s/단계)

| 단계 | 목표 연결 | 실제 연결 | [ERROR] 수 | Server WS(MB) | Server Handles | RTT avg | 판정 |
|------|-----------|-----------|------------|---------------|----------------|---------|------|
| 10   | 10        | 10        | 0          | 22.1          | 146            | 1ms     | **PASS** |
| 100  | 100       | 100       | 0          | 23.8          | 236            | 0ms     | **PASS** |
| 500  | 500       | 500       | 0          | 31.2          | 636            | 0ms     | **PASS** |
| 1000 | 1000      | 564       | 2607       | 31.7          | 648            | 0ms     | **FAIL** (WSA 10055) |

### Debug vs Release 비교 (500 clients 기준)

| 항목 | Debug (02-27) | Release (02-28) | 변화 |
|------|--------------|-----------------|------|
| Server WS | 44.9 MB | 31.2 MB | **-30.5%** |
| Server Handles | 662 | 636 | -3.9% |
| 최대 안정 동접 | 500 | 500 | 동일 |
| RTT avg (1 client) | N/A | ~0ms | — |

### 이번 실행에서 확인된 사항

1. **1000 클라이언트 WSA 10055 — Release에서도 재현**
   - 실제 연결: 564/1000, [ERROR]: 2607
   - Debug(02-27) 534/1000 과 유사한 패턴 → 빌드 수준 문제 아님
   - RIO request queue 생성 시스템 레벨 한계 → 별도 분석 필요

2. **WAL=Clean 확인** — 직전 Graceful Shutdown 완전 처리 확인됨

3. **스크립트 인코딩 버그 수정** — Write-Log에서 BOM 없는 UTF-8 로 수정 완료

### 상세 로그 위치

`Doc/Performance/Logs/20260228_124350/`

---

## 다음 측정 예정 항목

| 항목 | 우선순위 | 현재 상태 |
|------|----------|-----------|
| Sustain 30분 (500 clients) | High | 미측정 |
| RIO 1000+ WSA 10055 원인 분석 | High | FAIL 2회 재현 (Debug/Release 동일) |
| IOCP vs RIO 비교 (`--backend` 옵션 추가 후) | Medium | 코드 미구현 |
| Churn (연결/해제 반복 5회) | Medium | 미측정 |
| PC 강제종료 후 WAL Recover 확인 | Low | Clean만 확인됨 |

---

---

## ?ㅽ뻾: 20260228_172523

- **鍮뚮뱶**: x64 Release
- **?쒓컖**: 2026-02-28 17:25:23
- **Phase**: all
- **Ramp ?④퀎**: 10, 100, 500, 1000 clients
- **媛??④퀎 ?좎?**: 30珥?n
### Phase 0 ??Smoke Test (Release, 1 client, 10s)

| ??ぉ | 媛?|
|------|-----|
| 寃곌낵 | **PASS** |
| ?곌껐 ??| 1 |
| RTT min=7ms avg=7ms max=7ms Pong=1 | |
| ?쒕쾭 由ъ냼??| WS=178.8MB Handles=137 Threads=28 |
| DB 由ъ냼??| WS=37.6MB Handles=132 Threads=29 |
| [ERROR] ??| 0 |
| ?쒕쾭 ?뺤긽 醫낅즺 | True |
| ?대씪?댁뼵???뺤긽 醫낅즺 | True |

### Phase 1 ???덉젙???뚯뒪??n
#### 1-A: Graceful Shutdown (2 clients, 30s)

| ??ぉ | 媛?|
|------|-----|
| 寃곌낵 | **PASS** |
| ?곌껐???대씪?댁뼵????| 2 |
| ?쒕쾭 由ъ냼??(醫낅즺 吏곸쟾) | WS=178.8MB Handles=138 Threads=26 |
| DBTaskQueue ?쒕젅??| Yes |
| [ERROR] ??| 0 |

#### 1-B: Forced Shutdown + WAL Recovery

| ??ぉ | 媛?|
|------|-----|
| WAL ?곹깭 (?ш린???? | Clean |
| ?대씪?댁뼵???먮룞 ?ъ뿰寃??쒕룄 | Yes |

### Phase 2 ???쇳룷癒쇱뒪 Ramp-up (x64 Release, 30s/?④퀎)

| ?④퀎 | 紐⑺몴 ?곌껐 | ?ㅼ젣 ?곌껐 | [ERROR] ??| Server WS(MB) | Server Handles | RTT (?대씪?댁뼵??1踰? | ?먯젙 |
|------|-----------|-----------|------------|---------------|----------------|----------------------|------|
| 10 | 10 | 10 | 0 | 179 | 146 | RTT min=0ms avg=1ms max=3ms Pong=5 | **PASS** |
| 100 | 100 | 100 | 0 | 173.2 | 236 | RTT min=0ms avg=1ms max=6ms Pong=6 | **PASS** |
| 500 | 500 | 500 | 0 | 188.2 | 636 | RTT min=0ms avg=0ms max=1ms Pong=8 | **PASS** |
| 1000 | 1000 | 1000 | 0 | 193.8 | 937 | RTT min=0ms avg=0ms max=3ms Pong=15 | **PASS** |

### ?대쾲 ?ㅽ뻾 ?곸꽭 濡쒓렇 ?꾩튂

`C:\MyGithub\PublicStudy\NetworkModuleTest\Doc\Performance\Logs\20260228_172523`

---

---

## ?ㅽ뻾: 20260301_111832

- **鍮뚮뱶**: x64 Release
- **?쒓컖**: 2026-03-01 11:18:32
- **Phase**: all
- **Ramp ?④퀎**: 10, 100, 500, 1000 clients
- **媛??④퀎 ?좎?**: 30珥?n
### Phase 0 ??Smoke Test (Release, 1 client, 10s)

| ??ぉ | 媛?|
|------|-----|
| 寃곌낵 | **PASS** |
| ?곌껐 ??| 1 |
| RTT min=0ms avg=3ms max=6ms Pong=2 | |
| ?쒕쾭 由ъ냼??| WS=178.8MB Handles=137 Threads=28 |
| DB 由ъ냼??| WS=37.6MB Handles=132 Threads=29 |
| [ERROR] ??| 0 |
| ?쒕쾭 ?뺤긽 醫낅즺 | True |
| ?대씪?댁뼵???뺤긽 醫낅즺 | True |

### Phase 1 ???덉젙???뚯뒪??n
#### 1-A: Graceful Shutdown (2 clients, 30s)

| ??ぉ | 媛?|
|------|-----|
| 寃곌낵 | **PASS** |
| ?곌껐???대씪?댁뼵????| 2 |
| ?쒕쾭 由ъ냼??(醫낅즺 吏곸쟾) | WS=178.7MB Handles=138 Threads=26 |
| DBTaskQueue ?쒕젅??| Yes |
| [ERROR] ??| 0 |

#### 1-B: Forced Shutdown + WAL Recovery

| ??ぉ | 媛?|
|------|-----|
| WAL ?곹깭 (?ш린???? | Clean |
| ?대씪?댁뼵???먮룞 ?ъ뿰寃??쒕룄 | Yes |

### Phase 2 ???쇳룷癒쇱뒪 Ramp-up (x64 Release, 30s/?④퀎)

| ?④퀎 | 紐⑺몴 ?곌껐 | ?ㅼ젣 ?곌껐 | [ERROR] ??| Server WS(MB) | Server Handles | RTT (?대씪?댁뼵??1踰? | ?먯젙 |
|------|-----------|-----------|------------|---------------|----------------|----------------------|------|
| 10 | 10 | 10 | 0 | 178.9 | 146 | RTT min=0ms avg=1ms max=6ms Pong=5 | **PASS** |
| 100 | 100 | 100 | 0 | 180.6 | 236 | RTT min=0ms avg=1ms max=5ms Pong=6 | **PASS** |
| 500 | 500 | 500 | 0 | 188 | 636 | RTT min=0ms avg=0ms max=3ms Pong=8 | **PASS** |
| 1000 | 1000 | 1000 | 0 | 193.7 | 936 | RTT min=0ms avg=0ms max=1ms Pong=15 | **PASS** |

### ?대쾲 ?ㅽ뻾 ?곸꽭 濡쒓렇 ?꾩튂

`C:\MyGithub\PublicStudy\NetworkModuleTest\Doc\Performance\Logs\20260301_111832`

---

---

## ?ㅽ뻾: 20260301_163405

- **鍮뚮뱶**: x64 Release
- **?쒓컖**: 2026-03-01 16:34:05
- **Phase**: all
- **Ramp ?④퀎**: 10, 100, 500, 1000 clients
- **媛??④퀎 ?좎?**: 30珥?n
### Phase 0 ??Smoke Test (Release, 1 client, 10s)

| ??ぉ | 媛?|
|------|-----|
| 寃곌낵 | **PASS** |
| ?곌껐 ??| 1 |
| RTT min=4ms avg=4ms max=4ms Pong=1 | |
| ?쒕쾭 由ъ냼??| WS=178.9MB Handles=137 Threads=28 |
| DB 由ъ냼??| WS=37.6MB Handles=132 Threads=29 |
| [ERROR] ??| 0 |
| ?쒕쾭 ?뺤긽 醫낅즺 | True |
| ?대씪?댁뼵???뺤긽 醫낅즺 | True |

### Phase 1 ???덉젙???뚯뒪??n
#### 1-A: Graceful Shutdown (2 clients, 30s)

| ??ぉ | 媛?|
|------|-----|
| 寃곌낵 | **PASS** |
| ?곌껐???대씪?댁뼵????| 2 |
| ?쒕쾭 由ъ냼??(醫낅즺 吏곸쟾) | WS=178.7MB Handles=138 Threads=26 |
| DBTaskQueue ?쒕젅??| Yes |
| [ERROR] ??| 0 |

#### 1-B: Forced Shutdown + WAL Recovery

| ??ぉ | 媛?|
|------|-----|
| WAL ?곹깭 (?ш린???? | Clean |
| ?대씪?댁뼵???먮룞 ?ъ뿰寃??쒕룄 | Yes |

### Phase 2 ???쇳룷癒쇱뒪 Ramp-up (x64 Release, 30s/?④퀎)

| ?④퀎 | 紐⑺몴 ?곌껐 | ?ㅼ젣 ?곌껐 | [ERROR] ??| Server WS(MB) | Server Handles | RTT (?대씪?댁뼵??1踰? | ?먯젙 |
|------|-----------|-----------|------------|---------------|----------------|----------------------|------|
| 10 | 10 | 10 | 0 | 179 | 146 | RTT min=0ms avg=1ms max=5ms Pong=5 | **PASS** |
| 100 | 100 | 100 | 0 | 180.6 | 236 | RTT min=0ms avg=2ms max=8ms Pong=6 | **PASS** |
| 500 | 500 | 500 | 0 | 188.2 | 636 | RTT min=0ms avg=0ms max=1ms Pong=8 | **PASS** |
| 1000 | 1000 | 1000 | 0 | 143.6 | 853 | RTT min=0ms avg=2ms max=20ms Pong=16 | **PASS** |

### ?대쾲 ?ㅽ뻾 ?곸꽭 濡쒓렇 ?꾩튂

`C:\MyGithub\PublicStudy\NetworkModuleTest\Doc\Performance\Logs\20260301_163405`

---

---

## ?ㅽ뻾: 20260301_224424

- **鍮뚮뱶**: x64 Release
- **?쒓컖**: 2026-03-01 22:44:24
- **Phase**: 0
- **Ramp ?④퀎**: 10, 100, 500, 1000 clients
- **媛??④퀎 ?좎?**: 30珥?n
### Phase 0 ??Smoke Test (Release, 1 client, 10s)

| ??ぉ | 媛?|
|------|-----|
| 寃곌낵 | **PASS** |
| ?곌껐 ??| 1 |
| RTT min=4ms avg=4ms max=4ms Pong=1 | |
| ?쒕쾭 由ъ냼??| WS=342.5MB Handles=141 Threads=28 |
| DB 由ъ냼??| WS=54.2MB Handles=136 Threads=29 |
| [ERROR] ??| 0 |
| ?쒕쾭 ?뺤긽 醫낅즺 | True |
| ?대씪?댁뼵???뺤긽 醫낅즺 | True |

### ?대쾲 ?ㅽ뻾 ?곸꽭 濡쒓렇 ?꾩튂

`C:\MyGithub\PublicStudy\NetworkModuleTest\Doc\Performance\Logs\20260301_224424`

---

---

## ?ㅽ뻾: 20260301_224451

- **鍮뚮뱶**: x64 Release
- **?쒓컖**: 2026-03-01 22:44:51
- **Phase**: 2
- **Ramp ?④퀎**: 10 clients
- **媛??④퀎 ?좎?**: 30珥?n
### Phase 2 ???쇳룷癒쇱뒪 Ramp-up (x64 Release, 30s/?④퀎)

| ?④퀎 | 紐⑺몴 ?곌껐 | ?ㅼ젣 ?곌껐 | [ERROR] ??| Server WS(MB) | Server Handles | RTT (?대씪?댁뼵??1踰? | ?먯젙 |
|------|-----------|-----------|------------|---------------|----------------|----------------------|------|
| 10 | 10 | 10 | 0 | 342.4 | 150 | RTT min=0ms avg=0ms max=1ms Pong=5 | **PASS** |

### ?대쾲 ?ㅽ뻾 ?곸꽭 濡쒓렇 ?꾩튂

`C:\MyGithub\PublicStudy\NetworkModuleTest\Doc\Performance\Logs\20260301_224451`

---

---

## ?ㅽ뻾: 20260301_224544

- **鍮뚮뱶**: x64 Release
- **?쒓컖**: 2026-03-01 22:45:44
- **Phase**: 1
- **Ramp ?④퀎**: 10, 100, 500, 1000 clients
- **媛??④퀎 ?좎?**: 30珥?n
### Phase 1 ???덉젙???뚯뒪??n
#### 1-A: Graceful Shutdown (2 clients, 30s)

| ??ぉ | 媛?|
|------|-----|
| 寃곌낵 | **PASS** |
| ?곌껐???대씪?댁뼵????| 2 |
| ?쒕쾭 由ъ냼??(醫낅즺 吏곸쟾) | WS=342.4MB Handles=142 Threads=26 |
| DBTaskQueue ?쒕젅??| Yes |
| [ERROR] ??| 0 |

#### 1-B: Forced Shutdown + WAL Recovery

| ??ぉ | 媛?|
|------|-----|
| WAL ?곹깭 (?ш린???? | Clean |
| ?대씪?댁뼵???먮룞 ?ъ뿰寃??쒕룄 | Yes |

### ?대쾲 ?ㅽ뻾 ?곸꽭 濡쒓렇 ?꾩튂

`C:\MyGithub\PublicStudy\NetworkModuleTest\Doc\Performance\Logs\20260301_224544`

---

---

## ?ㅽ뻾: 20260302_023702

- **鍮뚮뱶**: x64 Release
- **?쒓컖**: 2026-03-02 02:37:02
- **Phase**: all
- **Ramp ?④퀎**: 10, 100, 500, 1000 clients
- **媛??④퀎 ?좎?**: 30珥?n
### Phase 0 ??Smoke Test (Release, 1 client, 10s)

| ??ぉ | 媛?|
|------|-----|
| 寃곌낵 | **FAIL** |
| ?곌껐 ??| 1 |
| RTT min=1ms avg=4ms max=7ms Pong=2 | |
| ?쒕쾭 由ъ냼??| WS=352.2MB Handles=138 Threads=29 |
| DB 由ъ냼??| WS=62.2MB Handles=133 Threads=30 |
| [ERROR] ??| 0 |
| ?쒕쾭 ?뺤긽 醫낅즺 | False |
| ?대씪?댁뼵???뺤긽 醫낅즺 | True |


---

## ?ㅽ뻾: 20260302_024028

- **鍮뚮뱶**: x64 Release
- **?쒓컖**: 2026-03-02 02:40:28
- **Phase**: all
- **Ramp ?④퀎**: 10, 100, 500, 1000 clients
- **媛??④퀎 ?좎?**: 30珥?n
### Phase 0 ??Smoke Test (Release, 1 client, 10s)

| ??ぉ | 媛?|
|------|-----|
| 寃곌낵 | **PASS** |
| ?곌껐 ??| 1 |
| RTT min=6ms avg=6ms max=6ms Pong=1 | |
| ?쒕쾭 由ъ냼??| WS=352.1MB Handles=138 Threads=29 |
| DB 由ъ냼??| WS=62.2MB Handles=133 Threads=30 |
| [ERROR] ??| 0 |
| ?쒕쾭 ?뺤긽 醫낅즺 | True |
| ?대씪?댁뼵???뺤긽 醫낅즺 | True |

### Phase 1 ???덉젙???뚯뒪??n
#### 1-A: Graceful Shutdown (2 clients, 30s)

| ??ぉ | 媛?|
|------|-----|
| 寃곌낵 | **PASS** |
| ?곌껐???대씪?댁뼵????| 2 |
| ?쒕쾭 由ъ냼??(醫낅즺 吏곸쟾) | WS=266.8MB Handles=139 Threads=27 |
| DBTaskQueue ?쒕젅??| Yes |
| [ERROR] ??| 0 |

#### 1-B: Forced Shutdown + WAL Recovery

| ??ぉ | 媛?|
|------|-----|
| WAL ?곹깭 (?ш린???? | Clean |
| ?대씪?댁뼵???먮룞 ?ъ뿰寃??쒕룄 | Yes |

### Phase 2 ???쇳룷癒쇱뒪 Ramp-up (x64 Release, 30s/?④퀎)

| ?④퀎 | 紐⑺몴 ?곌껐 | ?ㅼ젣 ?곌껐 | [ERROR] ??| Server WS(MB) | Server Handles | RTT (?대씪?댁뼵??1踰? | ?먯젙 |
|------|-----------|-----------|------------|---------------|----------------|----------------------|------|
| 10 | 10 | 10 | 0 | 351.9 | 147 | RTT min=0ms avg=1ms max=6ms Pong=5 | **PASS** |
| 100 | 100 | 100 | 0 | 352.7 | 237 | RTT min=0ms avg=1ms max=6ms Pong=6 | **PASS** |
| 500 | 500 | 500 | 0 | 356.2 | 638 | RTT min=0ms avg=1ms max=5ms Pong=8 | **PASS** |
| 1000 | 1000 | 1285 | 0 | 330.2 | 840 | RTT min=0ms avg=2ms max=24ms Pong=17 | **PASS** |

### ?대쾲 ?ㅽ뻾 ?곸꽭 濡쒓렇 ?꾩튂

`C:\MyGithub\PublicStudy\NetworkModuleTest\Doc\Performance\Logs\20260302_024028`

---
