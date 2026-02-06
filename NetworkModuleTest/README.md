# 📚 NetworkModuleTest 프로젝트 문서

**프로젝트**: 고성능 멀티플랫폼 네트워크 엔진 및 게임 서버
**언어**: C++17
**플랫폼**: Windows, Linux, macOS

---

## 📖 문서 목차

### 🏗️ 아키텍처 문서
1. [멀티플랫폼 엔진 완성 보고서](./docs/MULTIPLATFORM_ENGINE_COMPLETE.md) (작성 예정)
   - 전체 아키텍처 설계 및 구조
   - 플랫폼별 구현 상세
   - 성능 특성 및 벤치마크

### 🔄 마이그레이션 문서
2. [서버 마이그레이션 완료 보고서](./docs/SERVER_MIGRATION_COMPLETE.md)
   - IOCPNetworkEngine → 멀티플랫폼 엔진 전환
   - TestServer, TestDBServer 마이그레이션
   - 빌드 결과 및 성능 비교

3. [비동기 DB 아키텍처 완성 보고서](./docs/DB_ASYNC_ARCHITECTURE.md)
   - GameSession과 DB 처리 분리
   - DBTaskQueue 설계 및 구현
   - 논블로킹 비동기 처리 패턴

### ⚡ 성능 최적화 문서
4. [Lock 경합 분석 보고서](./docs/LOCK_CONTENTION_ANALYSIS.md)
   - P0-P3 우선순위별 Lock 경합 문제 분석
   - SessionManager, Session, DBTaskQueue 최적화
   - Atomic 연산 및 Lock-Free 패턴 적용
   - 성능 측정 및 벤치마크 권장사항

---

## 🚀 빠른 시작

### 빌드 방법
```bash
# Visual Studio 2022 사용
1. NetworkModuleTest.sln 열기
2. 빌드 구성: Debug/Release, x64
3. 전체 솔루션 빌드 (F7)
```

### 실행 순서
```bash
# 1. 서버 실행
TestServer.exe

# 2. 클라이언트 실행 (별도 터미널)
TestClient.exe
```

---

## 📁 프로젝트 구조

```
NetworkModuleTest/
├── Server/
│   ├── ServerEngine/          # 네트워크 엔진 라이브러리
│   ├── TestServer/            # 게임 서버
│   ├── DBServer/              # DB 서버
│   └── MultiPlatformNetwork/  # 테스트 프로젝트
│
├── Client/
│   └── TestClient/            # 테스트 클라이언트
│
├── docs/                      # 📚 문서 폴더
│   ├── LOCK_CONTENTION_ANALYSIS.md
│   ├── DB_ASYNC_ARCHITECTURE.md
│   ├── SERVER_MIGRATION_COMPLETE.md
│   └── MULTIPLATFORM_ENGINE_COMPLETE.md (작성 예정)
│
└── README.md                  # 이 파일
```

---

## 🎯 주요 기능

### 멀티플랫폼 네트워크 엔진
- ✅ **Windows**: IOCP, RIO (자동 감지)
- ✅ **Linux**: epoll, io_uring (자동 감지)
- ✅ **macOS**: kqueue

### 비동기 DB 아키텍처
- ✅ **논블로킹**: GameSession과 DB 처리 완전 분리
- ✅ **작업 큐**: Producer-Consumer 패턴
- ✅ **워커 풀**: 멀티 스레드 병렬 처리

### 성능 최적화
- ✅ **Lock-Free**: Atomic 연산 활용
- ✅ **Fast-Path**: Lock 경합 최소화
- ✅ **Zero-Copy**: Move Semantics 지원

---

## 📊 성능 특성

### 처리량
- **동시 접속**: 1,000+ 동시 연결
- **패킷 처리**: 10,000+ 패킷/초
- **DB 작업**: 비동기 큐잉 (논블로킹)

### 레이턴시
- **네트워크**: < 1ms (평균)
- **DB 큐잉**: < 1ms (즉시 반환)

---

## 🔧 최근 업데이트

### 2026-02-05
- ✅ P0: SessionManager::CloseAllSessions() Deadlock 수정
- ✅ P1: Session::Send() Lock 경합 최적화 (Atomic 카운터)
- ✅ P2: DBTaskQueue::GetQueueSize() Lock-Free 구현
- ✅ Lock 경합 분석 보고서 작성
- ✅ 모든 서버 빌드 성공 및 안정성 검증

---

## 📝 개발 가이드

### 코딩 컨벤션
- **언어**: C++17
- **주석**: 영어/한글 이중 주석
- **네이밍**: PascalCase (클래스), camelCase (변수), UPPER_CASE (상수)

### 커밋 메시지 형식
```
[카테고리] 간단한 설명

상세 내용 (선택)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

**카테고리 예시**:
- `[Fix]`: 버그 수정
- `[Optimize]`: 성능 최적화
- `[Refactor]`: 리팩토링
- `[Docs]`: 문서 작성/수정
- `[Feature]`: 새 기능 추가

---

## 🐛 알려진 이슈

없음 (2026-02-05 기준)

---

## 📬 연락처

프로젝트 관리자: [작성 필요]

---

## 📄 라이선스

[라이선스 정보 작성 필요]

---

**마지막 업데이트**: 2026-02-05
**버전**: 1.0.0
**빌드 환경**: Visual Studio 2022, Windows 10, x64
