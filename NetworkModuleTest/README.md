# 📚 NetworkModuleTest 프로젝트 문서

**프로젝트**: 고성능 멀티플랫폼 네트워크 엔진 및 게임 서버
**언어**: C++17
**플랫폼**: Windows (주 개발/테스트), Linux/macOS (스캐폴딩/실험)

---

## 📖 문서 목차

**전체 문서 인덱스**: [Doc/README.md](./Doc/README.md) - 모든 문서 목록 및 상세 설명

### 🎯 빠른 링크
- [프로젝트 개요](./Doc/01_ProjectOverview.md) - 프로젝트 소개 및 목표
- [전체 아키텍처](./Doc/02_Architecture.md) - 시스템 구조
- [코드-문서 통합 시각화](./Doc/07_VisualMap.md) - 구조/흐름 다이어그램
- [개발 가이드](./Doc/05_DevelopmentGuide.md) - 빌드 및 실행 방법
- [API 문서](./Doc/04_API.md) - API 사용법

### 🏗️ 심화 문서
- 상세/분석 문서는 [Doc/README.md](./Doc/README.md)에서 확인 (일부 문서 업데이트 필요)

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
# 1. DB 서버 실행
TestDBServer.exe

# 2. 서버 실행 (별도 터미널)
TestServer.exe

# 3. 클라이언트 실행 (별도 터미널)
TestClient.exe
```

---

## 📁 프로젝트 구조

```
NetworkModuleTest/
├── Server/
│   ├── ServerEngine/          # 네트워크 엔진 라이브러리
│   ├── TestServer/            # 게임 서버
│   └── DBServer/              # DB 서버
│
├── Client/
│   └── TestClient/            # 테스트 클라이언트
│
├── ModuleTest/                # 단위/통합 테스트 프로젝트
│   ├── MultiPlatformNetwork/  # 비동기 I/O 검증
│   ├── DBModuleTest/          # DB 모듈 검증
│   └── ServerStructureSync/   # 서버 구조 동기화 검증
│
├── Doc/                       # 📚 문서 폴더
│   ├── 01_ProjectOverview.md
│   ├── 02_Architecture.md
│   ├── 07_VisualMap.md
│   ├── Architecture/          # 아키텍처 상세
│   ├── Performance/           # 성능 최적화
│   ├── Database/              # DB 문서
│   ├── Network/               # 네트워크 문서
│   ├── Development/           # 개발 가이드
│   └── README.md              # 문서 인덱스
│
└── README.md                  # 이 파일
```

---

## 🎯 주요 기능

### 멀티플랫폼 네트워크 엔진
- ✅ **Windows**: IOCP, RIO (자동 감지, 주 개발/테스트)
- ⚠️ **Linux**: epoll, io_uring (기본 send/recv 구현, 테스트 필요)
- ⚠️ **macOS**: kqueue (기본 send/recv 구현, 테스트 필요)

### 비동기 DB 아키텍처
- ✅ **논블로킹**: ClientSession과 DB 처리 분리
- ✅ **작업 큐**: Producer-Consumer 패턴
- ⚠️ **DB 저장**: 현재는 로깅/플레이스홀더 중심 (실DB 연동은 TODO)

### 성능 최적화
- ✅ **Lock-Free**: Atomic 연산 활용
- ✅ **Fast-Path**: Lock 경합 최소화
- ✅ **Zero-Copy**: Move Semantics 지원

---

## 📊 성능 특성 (목표/가정)

### 처리량
- **동시 접속**: 1,000+ 동시 연결
- **패킷 처리**: 10,000+ 패킷/초
- **DB 작업**: 비동기 큐잉 (논블로킹)

### 레이턴시
- **네트워크**: < 1ms (평균)
- **DB 큐잉**: < 1ms (즉시 반환)

---

## 🔧 최근 업데이트

### 2026-02-25
- ✅ AsyncBufferPool 통합 (RIOBufferPool/IOUringBufferPool → using alias로 단일화)
- ✅ RIOTest, IOUringTest 독립 VS 프로젝트로 승격
- ✅ 플랫폼별 버퍼 할당 로직 AllocAligned/FreeAligned로 격리

### 2026-02-10
- ✅ TestServer ↔ TestDBServer 패킷 연결 경로 추가
- ✅ Linux/macOS 기본 send/recv 경로 보강
- ✅ 문서 심화 영역 최신화
- ✅ 코드-문서 통합 시각화 문서 추가 (`Doc/07_VisualMap.md`)

### 2026-02-09
- ✅ 문서-코드 정합성 점검 및 주요 문서 정리
- ✅ TestDBServer 기본 포트/옵션 정보 갱신
- ✅ 멀티플랫폼 지원 상태 명시 (Windows 중심, Linux/macOS 스캐폴딩)

### 2026-02-06
- ✅ 문서 구조 대대적 개편 (Doc/ 폴더로 통합)
- ✅ 임시/중복 문서 제거 (25개 파일)
- ✅ 카테고리별 분류 및 문서 인덱스 생성
- ✅ README.md 업데이트 (최신 문서 링크 반영)

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

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
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

**마지막 업데이트**: 2026-02-10
**버전**: 2.0.0 (문서 구조 개편)
**빌드 환경**: Visual Studio 2022, Windows 10, x64
