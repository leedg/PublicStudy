# ServerEngine 네트워크 부하테스트 체크리스트

**Version**: 1.0  
**Date**: 2026-02-26  
**Status**: Ready to Run  
**Target**: `Server/ServerEngine`, `Server/TestServer`, `Client/TestClient`

---

## 1. 테스트 목표 정의

- [ ] 동시 접속 목표치 정의: `N clients`
- [ ] 처리량 목표 정의: `pkt/s`, `MB/s`
- [ ] 지연 목표 정의: `p95`, `p99` (ms)
- [ ] 오류율 목표 정의: `%` (send/recv 실패, disconnect)
- [ ] 성공 기준(Pass/Fail) 문서화

### 권장 초기 기준 (예시)

- [ ] `p99 < 50ms`
- [ ] `오류율 < 0.1%`
- [ ] `30분 sustain 중 세션 누수 없음`

---

## 2. 환경 고정

- [ ] 빌드 설정: `x64 Release`
- [ ] 서버/클라이언트 실행 머신 스펙 기록 (CPU, RAM, NIC)
- [ ] 포트 충돌 점검 (`9000`, `8002` 기본)
- [ ] 백그라운드 고부하 프로세스 종료
- [ ] Windows 전원 옵션 고성능 설정 확인

---

## 3. 사전 빌드/기동 확인

- [ ] 솔루션 빌드 성공

```powershell
msbuild NetworkModuleTest.sln /p:Configuration=Release /p:Platform=x64
```

- [ ] 기본 프로세스 기동 순서 검증: `DBServer -> TestServer -> TestClient`

```powershell
.\run_dbServer.ps1 -DbPort 8002
.\run_server.ps1 -ServerPort 9000 -DbPort 8002
.\run_client.ps1
```

- [ ] 빠른 자동 테스트 1회 통과

```powershell
.\run_test_auto.ps1 -RunSeconds 5
```

---

## 4. 측정 항목 준비

- [ ] 초당 연결 수 (`accept/s`)
- [ ] 송수신 바이트 (`send bytes/s`, `recv bytes/s`)
- [ ] 송수신 패킷 (`send pkt/s`, `recv pkt/s`)
- [ ] 활성 세션 수, disconnect 수
- [ ] 큐 적체 크기 (DBTaskQueue 포함)
- [ ] 오류 코드 분포 (`WSAECONNREFUSED` 포함)
- [ ] CPU/메모리/핸들 수

---

## 5. 시나리오 실행 순서

## 5.1 Baseline (무부하/저부하)
- [ ] 1~10 클라이언트
- [ ] 64B 소형 패킷
- [ ] 2~5분 실행

## 5.2 Ramp-up (점진 증가)
- [ ] `100 -> 500 -> 1000 -> 3000 -> 5000` 단계별 증가
- [ ] 단계마다 3~5분 유지
- [ ] 각 단계 종료 시 p95/p99, 오류율 기록

## 5.3 Sustain (장시간 유지)
- [ ] 목표 동접에서 30분 유지
- [ ] 메모리 증가 추세(누수 의심) 기록
- [ ] 스레드/핸들 수 고정성 확인

## 5.4 Burst (순간 급증)
- [ ] 짧은 시간(10~30초) 트래픽 2~3배
- [ ] 복구 시간 (`T_recover`) 측정

## 5.5 Churn (연결/해제 반복)
- [ ] 연결/해제 반복 비율 정의 (예: 초당 100 connect/disconnect)
- [ ] 세션 정리 누락 여부 확인

## 5.6 Reconnect (서버 재기동/장애)
- [ ] 서버 재기동 후 클라이언트 재연결 성공률 기록
- [ ] `WSAECONNREFUSED` 구간 1초 재시도 확인
- [ ] 기타 오류 지수 백오프(최대 30초) 동작 확인

---

## 6. 패킷 프로파일

- [ ] 소형: `32B~128B`
- [ ] 중형: `512B~1KB`
- [ ] 대형: `4KB+`
- [ ] 혼합 비율: `70/25/5` (소/중/대)

---

## 7. 종료/정리 검증 (Graceful Shutdown)

- [ ] 세션 전체 종료 확인
- [ ] 큐 드레인 확인
- [ ] 워커/스레드 join 완료 확인
- [ ] 종료 후 프로세스 잔존 여부 확인

---

## 8. 결과 기록 규칙

- [ ] 시나리오별 로그 파일명 통일: `YYYYMMDD_HHMM_<scenario>.md`
- [ ] 동일 시나리오 최소 3회 반복 측정
- [ ] 최고값 1회가 아닌 중앙값/분위수 기준으로 판정
- [ ] 이상치(outlier) 발생 시 원인/재현 조건 기록

---

## 9. 합격 판정

- [ ] 목표 동접 달성
- [ ] 목표 처리량 달성
- [ ] p99 기준 충족
- [ ] 오류율 기준 충족
- [ ] 장시간 누수/불안정 없음

Pass/Fail: `__________`  
판정 근거 요약: `____________________________________________`

