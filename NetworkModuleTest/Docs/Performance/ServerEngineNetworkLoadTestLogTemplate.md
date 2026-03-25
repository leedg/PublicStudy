# ServerEngine 부하테스트 로그 템플릿

복사해서 파일명만 바꿔 사용:

`Doc/Performance/Logs/YYYYMMDD_HHMM_<scenario>.md`

---

## 1. 실행 정보

- 테스트 일시: `YYYY-MM-DD HH:MM`
- 실행자: ``
- 브랜치/커밋: ``
- OS: ``
- 서버 머신: `CPU / RAM / NIC`
- 클라이언트 머신: `CPU / RAM / NIC`
- 빌드: `x64 Release`
- 실행 스크립트:
  - `.\run_dbServer.ps1 -DbPort 8002`
  - `.\run_server.ps1 -ServerPort 9000 -DbPort 8002`
  - `.\run_test_auto.ps1 -RunSeconds <N>`

---

## 2. 시나리오 정의

- 시나리오 이름: `Baseline | Ramp-up | Sustain | Burst | Churn | Reconnect`
- 목표 동시 접속: ``
- 실행 시간: ``
- 패킷 크기/분포: `예: 64B / 512B / 4KB = 70 / 25 / 5`
- 트래픽 패턴: `고정 | 점증 | 급증 | 연결/해제 반복`

---

## 3. 관측 메트릭

| 항목 | 측정값 | 목표 | 결과 |
|------|--------|------|------|
| Peak 동시접속 |  |  | Pass/Fail |
| 평균 처리량 (pkt/s) |  |  | Pass/Fail |
| 평균 처리량 (MB/s) |  |  | Pass/Fail |
| p95 latency (ms) |  |  | Pass/Fail |
| p99 latency (ms) |  |  | Pass/Fail |
| 오류율 (%) |  |  | Pass/Fail |
| disconnect 횟수 |  |  | Pass/Fail |
| CPU 사용률 (%) |  |  | Pass/Fail |
| 메모리 사용량 (MB) |  |  | Pass/Fail |
| 핸들/스레드 수 변화 |  |  | Pass/Fail |

---

## 4. 이벤트/오류 로그

| 시각 | 컴포넌트 | 이벤트 | 에러코드/메시지 | 영향도 |
|------|----------|--------|-----------------|--------|
|  | TestServer |  |  | Low/Medium/High |
|  | DBServer |  |  | Low/Medium/High |
|  | Client |  |  | Low/Medium/High |

---

## 5. 시나리오별 관찰 노트

- 정상 동작:
  - ``
- 성능 저하 구간:
  - ``
- 재현 조건:
  - ``
- 추정 병목:
  - ``

---

## 6. 재연결/복구 검증 (해당 시)

- 서버 중단 시각: ``
- 재기동 시각: ``
- 재연결 완료 시각: ``
- 복구 시간 (`T_recover`): ``
- `WSAECONNREFUSED` 처리 확인: `Yes/No`
- 지수 백오프 동작 확인: `Yes/No`

---

## 7. Graceful Shutdown 체크

- 세션 전체 종료: `Yes/No`
- 큐 드레인 완료: `Yes/No`
- 스레드 join 완료: `Yes/No`
- 프로세스 잔존 없음: `Yes/No`

---

## 8. 최종 판정

- 종합 결과: `Pass / Conditional Pass / Fail`
- 근거 요약:
  - ``
- 다음 액션:
  1. ``
  2. ``
  3. ``

