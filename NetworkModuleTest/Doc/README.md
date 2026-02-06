# 📚 NetworkModuleTest 문서 인덱스

**프로젝트**: 고성능 멀티플랫폼 네트워크 엔진 및 게임 서버
**언어**: C++17
**플랫폼**: Windows, Linux, macOS

---

## 📖 주요 문서

### 🎯 시작하기
1. [프로젝트 개요](./01_ProjectOverview.md) - 프로젝트 소개 및 목표
2. [개발 가이드](./05_DevelopmentGuide.md) - 빌드 및 실행 방법
3. [솔루션 가이드](./06_SolutionGuide.md) - 솔루션 구조 및 사용법

### 🏗️ 아키텍처
4. [전체 아키텍처](./02_Architecture.md) - 시스템 전체 구조
5. [프로토콜 명세](./03_Protocol.md) - 통신 프로토콜
6. [API 문서](./04_API.md) - API 사용법

---

## 📁 상세 문서 (카테고리별)

### Architecture/ - 아키텍처 설계
- [**MultiplatformEngine.md**](./Architecture/MultiplatformEngine.md)
  - 멀티플랫폼 네트워크 엔진 완성 보고서
  - Windows (IOCP, RIO), Linux (epoll, io_uring), macOS (kqueue)
  - 플랫폼별 자동 감지 및 최적 백엔드 선택

- [**NetworkArchitecture.md**](./Architecture/NetworkArchitecture.md)
  - 네트워크 엔진 아키텍처 상세 설명
  - Session, SessionManager, AsyncIOProvider 구조

- [**ServerMigration.md**](./Architecture/ServerMigration.md)
  - IOCPNetworkEngine → 멀티플랫폼 엔진 마이그레이션
  - TestServer, TestDBServer 마이그레이션 과정
  - 빌드 결과 및 성능 비교

- [**AsyncDB.md**](./Architecture/AsyncDB.md)
  - 비동기 DB 아키텍처 설계
  - GameSession과 DB 처리 완전 분리
  - DBTaskQueue 및 워커 풀 구현

- [**CrossPlatform.md**](./Architecture/CrossPlatform.md)
  - 크로스 플랫폼 아키텍처 설계
  - 플랫폼 추상화 계층
  - 포팅 가이드

### Performance/ - 성능 최적화
- [**LockContentionAnalysis.md**](./Performance/LockContentionAnalysis.md)
  - P0-P3 우선순위별 Lock 경합 분석
  - SessionManager, Session, DBTaskQueue 최적화
  - Atomic 연산 및 Lock-Free 패턴
  - 성능 측정 및 벤치마크 전략

- [**Benchmarking.md**](./Performance/Benchmarking.md)
  - 성능 벤치마킹 가이드
  - 플랫폼별 성능 비교 방법
  - 부하 테스트 시나리오

### Database/ - 데이터베이스
- [**README.md**](./Database/README.md)
  - 데이터베이스 모듈 개요
  - ODBC, OLEDB 지원
  - ConnectionPool 사용법

- [**MigrationGuide.md**](./Database/MigrationGuide.md)
  - 데이터베이스 마이그레이션 가이드
  - 스키마 변경 및 데이터 이전

### Network/ - 네트워크
- [**IOCPAnalysis.md**](./Network/IOCPAnalysis.md)
  - Windows IOCP 아키텍처 분석
  - IOCP vs RIO 비교
  - 성능 특성 및 최적화 전략

- [**CodingConventions.md**](./Network/CodingConventions.md)
  - 네트워크 코드 작성 규칙
  - 네이밍 컨벤션
  - 코드 스타일 가이드

- [**APIDesign.md**](./Network/APIDesign.md)
  - 네트워크 API 설계 문서
  - INetworkEngine 인터페이스
  - AsyncIOProvider 구조

### Development/ - 개발 문서
- [**UnitTesting.md**](./Development/UnitTesting.md)
  - 유닛 테스트 전략
  - GTest 사용법
  - 테스트 시나리오 작성

- [**CMakeBuild.md**](./Development/CMakeBuild.md)
  - CMake 빌드 설정
  - 크로스 플랫폼 빌드
  - 의존성 관리

- [**NamingConventions.md**](./Development/NamingConventions.md)
  - 전체 프로젝트 네이밍 규칙
  - 클래스, 변수, 함수 명명 규칙
  - 파일 구조 규칙

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

### 2026-02-06
- ✅ 문서 구조 재정리 (Doc/ 폴더로 통합)
- ✅ 임시 문서 제거 (25개 파일)
- ✅ 카테고리별 분류 (Architecture, Performance, Database, Network, Development)
- ✅ 문서 인덱스 생성

### 2026-02-05
- ✅ P0: SessionManager::CloseAllSessions() Deadlock 수정
- ✅ P1: Session::Send() Lock 경합 최적화 (Atomic 카운터)
- ✅ P2: DBTaskQueue::GetQueueSize() Lock-Free 구현
- ✅ Lock 경합 분석 보고서 작성
- ✅ 모든 서버 빌드 성공 및 안정성 검증

---

## 📝 문서 작성 규칙

### 파일 네이밍
- 메인 문서: `01_ProjectOverview.md` (번호 + 제목)
- 카테고리 문서: `Category/FileName.md` (PascalCase)

### 문서 구조
```markdown
# 📚 제목

## 개요
간단한 설명

## 상세 내용
### 섹션 1
### 섹션 2

## 참고 자료
```

### 주석 규칙
- 영어/한글 이중 주석
- 코드 예제는 문법 강조 사용
- 다이어그램은 ASCII 또는 Mermaid

---

## 🔗 관련 링크

- [메인 README](../README.md) - 프로젝트 루트 문서
- [GitHub Repository](https://github.com/leedg/PublicStudy) - 소스 코드

---

**마지막 업데이트**: 2026-02-06
**버전**: 2.0.0
**관리자**: [작성 필요]
