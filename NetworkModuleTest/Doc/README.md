# Document Index & Quick Reference

**생성일**: 2026-01-27  
**상태**: Phase 1 (분석 및 계획 완료)

---

## 📚 문서 구조

### 1. 분석 문서

#### [01_IOCP_Architecture_Analysis.md](./01_IOCP_Architecture_Analysis.md)
**목적**: RAON Server Engine의 IOCP 구현 분석

**주요 내용**:
- ✅ IOCP 핵심 컴포넌트 (IocpCore, ServiceCoordinator, IocpObject)
- ✅ ServiceCoordinator의 역할 및 ServiceType 분류
- ✅ OverlappedEx 구조체 및 IO_TYPE 열거형 상세 설명
- ✅ IocpCore 동작 원리 (초기화, 서비스 등록, IOCP 루프)
- ✅ IocpObjectSession 생명주기 및 버퍼 관리
- ✅ SessionPool 순환 큐 구조
- ✅ IocpObjectListener (서버 리스너)
- ✅ 에러 처리 및 에지 케이스
- ✅ 멀티스레드 안전성 패턴
- ✅ 성능 최적화 고려사항

**권장 읽기 순서**:
1. Section 1 & 2 (개요)
2. Section 3 (IOCP 핵심)
3. Section 4-6 (세부 구현)
4. Section 7-9 (고급 주제)

---

### 2. 코딩 규칙 문서

#### [02_Coding_Conventions_Guide.md](./02_Coding_Conventions_Guide.md)
**목적**: 프로젝트 전체에 적용할 코딩 규칙

**주요 내용**:
- ✅ **Allman 스타일** (필수) - 모든 괄호
- ✅ 들여쓰기 규칙 (4칸 스페이스)
- ✅ 주석 규칙 (영문+한글, 모든 라인)
- ✅ 열거형 주석 (enum class, 값별 설명)
- ✅ 네이밍 컨벤션
  - 클래스/구조체: `PascalCase`
  - 멤버 변수: `mVariableName`
  - 함수/메서드: `PascalCase`
  - 로컬 변수: `camelCase`
  - 상수/매크로: `UPPER_CASE`
- ✅ 플랫폼별 코드 분기 (Windows/Linux/macOS)
- ✅ C++17/20 기능 활용
- ✅ 스마트 포인터 사용
- ✅ 타입 안전성
- ✅ 메모리 관리 (RAII)
- ✅ 함수 설계 및 예외 처리
- ✅ 로깅 규칙

**체크리스트**:
```
필수 규칙 (MUST):
☐ Allman 스타일 괄호
☐ 영문+한글 주석
☐ enum class 사용
☐ 네이밍 컨벤션 준수
☐ std::unique_ptr 사용
☐ const 정확성

강력 권장 (SHOULD):
☐ C++17/20 기능
☐ 범위 기반 for 루프
☐ std::atomic 동기화
☐ 단위 테스트

피해야 할 것 (MUST NOT):
☐ 1TBS 스타일
☐ 탭 들여쓰기
☐ 주석 없는 코드
☐ C 스타일 배열
```

---

### 3. 구현 로드맵

#### [03_Implementation_Roadmap.md](./03_Implementation_Roadmap.md)
**목적**: 프로젝트 개발 계획 및 일정

**주요 내용**:
- ✅ 프로젝트 구조 (디렉토리 레이아웃)
- ✅ 개발 단계 (6개 Phase, 12주)
  - Phase 1: 기본 구조 (Weeks 1-2)
  - Phase 2: Windows IOCP (Weeks 3-4)
  - Phase 3: Linux epoll (Weeks 5-6)
  - Phase 4: macOS kqueue (Week 7)
  - Phase 5: 고급 기능 (Weeks 8-10)
  - Phase 6: 문서화 및 테스트 (Weeks 11-12)
- ✅ 주요 클래스 설계
- ✅ 플랫폼별 전략
- ✅ 성능 최적화 전략
- ✅ 테스트 계획
- ✅ 위험 요소 및 대응

---

## 🎯 빠른 참조 (Quick Reference)

### IOCP 핵심 개념

| 개념 | 설명 | 파일 |
|------|------|------|
| **IocpCore** | IOCP 커널 객체 관리, GQCS/PQCS | 01 - Section 3 |
| **ServiceCoordinator** | 서비스 생명주기 관리 | 01 - Section 1.2 |
| **IocpObjectSession** | 세션 추상 클래스 | 01 - Section 4 |
| **SessionPool** | 세션 객체 풀 | 01 - Section 5 |
| **OverlappedEx** | IOCP 완료 정보 | 01 - Section 2.1 |
| **IO_TYPE** | IO 작업 타입 | 01 - Section 2.1 |

### 코딩 규칙

| 항목 | 규칙 | 파일 |
|------|------|------|
| **괄호** | Allman 스타일 | 02 - Section 1 |
| **들여쓰기** | 4칸 스페이스 | 02 - Section 2 |
| **주석** | 영문+한글 | 02 - Section 3 |
| **클래스** | PascalCase | 02 - Section 5 |
| **멤버 변수** | mVariableName | 02 - Section 5 |
| **함수** | PascalCase | 02 - Section 5 |
| **플랫폼 분기** | #ifdef with 주석 | 02 - Section 6 |
| **메모리** | std::unique_ptr | 02 - Section 9 |

### 개발 일정

| Phase | 기간 | 목표 | 파일 |
|-------|------|------|------|
| 1 | Weeks 1-2 | 프로젝트 구조 | 03 - Phase 1 |
| 2 | Weeks 3-4 | Windows IOCP | 03 - Phase 2 |
| 3 | Weeks 5-6 | Linux epoll | 03 - Phase 3 |
| 4 | Week 7 | macOS kqueue | 03 - Phase 4 |
| 5 | Weeks 8-10 | 고급 기능 | 03 - Phase 5 |
| 6 | Weeks 11-12 | 테스트 & 문서 | 03 - Phase 6 |

---

### 4. 참고 파일 가이드

#### [04_Reference_Files_Guide.md](./04_Reference_Files_Guide.md)
**목적**: RAON Server Engine 참고 파일 목록 및 우선순위

**주요 내용**:
- ✅ 21개 참고 파일 우선순위 (Tier 1-4)
- ✅ 파일별 역할 및 크기
- ✅ 핵심 멤버 변수 및 메서드 목록
- ✅ 의존성 맵 및 상호참조
- ✅ 7가지 코드 패턴 분석
- ✅ 7일 학습 경로

**학습 순서**:
```
Day 1-2: Tier 1 (기본 타입 및 추상화)
Day 3-4: Tier 2 (핵심 관리자)
Day 5-6: Tier 3 (세션 관리)
Day 7: Tier 4 (리스너, 버퍼, 기타)
```

---

### 5. RIO & IO_URING 마이그레이션 계획

#### [05_RIO_IO_URING_Migration_Plan.md](./05_RIO_IO_URING_Migration_Plan.md)
**목적**: Windows RIO + Linux io_uring 크로스 플랫폼 마이그레이션

**주요 내용**:
- ✅ RIO vs IOCP 비교 (성능, 메모리, API)
- ✅ io_uring vs epoll vs IOCP 비교
- ✅ RIO와 io_uring의 구조 유사성
- ✅ AsyncIOProvider 추상 인터페이스 설계
- ✅ 3가지 마이그레이션 옵션 (Option A/B/C)
  - **Option A** (권장): 최소 변경, 2주
  - **Option B**: 계층화 설계, 3주
  - **Option C**: 완전 재설계, 6-8주
- ✅ RAON 코드 변경점 상세
- ✅ 성능 영향 분석
- ✅ 위험 분석 및 완화
- ✅ 검증 전략
- ✅ 단계별 구현 계획 (8주)

**추천 접근법**:
```
Phase 1 (2주): AsyncIOProvider 래퍼 구현 (Option A)
Phase 2 (3주): Windows RIO 구현
Phase 3 (3주): Linux io_uring 구현
Phase 4 (2주): 최적화 및 테스트

예상 성능 향상: 2.8배 (IOCP 대비)
```

---

### 6. 크로스 플랫폼 아키텍처 설계

#### [06_Cross_Platform_Architecture.md](./06_Cross_Platform_Architecture.md)
**목적**: AsyncIOProvider 통일 인터페이스 및 플랫폼별 구현

**주요 내용**:
- ✅ 전체 아키텍처 (계층 구조, 디렉토리 구조)
- ✅ AsyncIOProvider 인터페이스 상세 (완전한 헤더 파일)
  - 초기화/정리 (Initialize, Shutdown)
  - 버퍼 관리 (RegisterBuffer, UnregisterBuffer)
  - 비동기 I/O (SendAsync, RecvAsync, FlushRequests)
  - 완료 처리 (ProcessCompletions)
  - 정보 조회 (GetInfo, GetStats, GetLastError)
- ✅ Windows RIO 구현 (RIOAsyncIOProvider)
  - RIO 함수 포인터 로드
  - 완료 큐/요청 큐 관리
  - 배치 처리 (RIOCommitSends)
- ✅ Linux io_uring 구현 (IOUringAsyncIOProvider)
  - Ring Buffer 관리
  - SQ/CQ 처리
  - Fixed Buffer 지원
- ✅ 플랫폼 선택 전략 (런타임 감지, 명시적 선택)
- ✅ 에러 처리 전략 (AsyncIOError enum, 복구 패턴)
- ✅ 메모리 관리 전략 (RIO 버퍼 풀, io_uring Fixed Buffer)
- ✅ 성능 최적화 가이드 (배치 크기, 타임아웃)
- ✅ 테스트 전략 (단위 테스트, 성능 테스트)

**구현 도움말**:
```cpp
// AsyncIOProvider 사용 예시
auto provider = CreateAsyncIOProvider();
provider->Initialize(4096, 10000);

provider->SendAsync(socket, data, size, context, 0);
provider->FlushRequests();

CompletionEntry entries[32];
int count = provider->ProcessCompletions(entries, 32, 1000);
```

---

## 📂 파일 위치

```
E:\MyGitHub\PublicStudy\NetworkModuleTest\Doc\
├── 01_IOCP_Architecture_Analysis.md      ✓ 완성 (16 KB)
├── 02_Coding_Conventions_Guide.md       ✓ 완성 (18 KB)
├── 03_Implementation_Roadmap.md         ✓ 완성 (13 KB)
├── 04_Reference_Files_Guide.md          ✓ 완성 (20 KB)
├── 05_RIO_IO_URING_Migration_Plan.md    ✓ 완성 (37 KB)
├── 06_Cross_Platform_Architecture.md    ✓ 완성 (38 KB)
├── ANALYSIS_SUMMARY.txt                 ✓ 완성 (9.2 KB)
├── README.md                            (현재 파일)
└── 총 용량: 180 KB
```

---

## 🚀 다음 단계 (Next Steps)

### Phase 2 진행 중 (진행률 50%)

**완료된 작업**:
1. ✅ RAON Server IOCP 구현 분석 (01_IOCP_Architecture_Analysis.md)
2. ✅ 코딩 컨벤션 가이드 작성 (02_Coding_Conventions_Guide.md)
3. ✅ 구현 로드맵 수립 (03_Implementation_Roadmap.md)
4. ✅ 참고 파일 가이드 작성 (04_Reference_Files_Guide.md)
5. ✅ RIO/io_uring 마이그레이션 계획 (05_RIO_IO_URING_Migration_Plan.md)
6. ✅ 크로스 플랫폼 아키텍처 설계 (06_Cross_Platform_Architecture.md)

**백그라운드 진행 중**:
- RIO API 상세 분석 (bg_5dba1243)
- io_uring 구조 분석 (bg_5edc3f5b)
- 프로덕션 코드 사례 수집 (bg_62e1204c)
- RAON 마이그레이션 포인트 분석 (bg_d4a8d5d0)

### 즉시 실행 (Immediate)

1. **문서 검토 및 팀 합의**
   - [ ] 05번 마이그레이션 계획 검토
   - [ ] 06번 아키텍처 설계 검토
   - [ ] Option A (권장) 승인

2. **AsyncIOProvider 구현 준비**
   - [ ] 백그라운드 에이전트 결과 수집
   - [ ] AsyncIOProvider.h 최종 확정
   - [ ] CMake 빌드 설정
   - [ ] 테스트 프레임워크 선택

3. **코드 기반 준비**
   - [ ] E:\MyGitHub\PublicStudy\NetworkModuleTest\Src 디렉토리 생성
   - [ ] AsyncIO/ 디렉토리 구조 생성
   - [ ] 헤더 파일 틀 작성

### 단기 (Week 1-2)

1. **AsyncIOProvider 구현**
   - [ ] AsyncIOProvider.h/cpp (추상 클래스)
   - [ ] IocpAsyncIOProvider (IOCP 래퍼)
   - [ ] RIOAsyncIOProvider (RIO 구현, 선택사항)

2. **통합 테스트**
   - [ ] 단위 테스트 작성
   - [ ] Windows IOCP 호환성 검증
   - [ ] 성능 벤치마크

### 중기 (Week 3-4)

1. **IocpCore 통합**
   - [ ] AsyncIOProvider 적용
   - [ ] IocpObjectSession 호환성 수정
   - [ ] 기존 코드 호환성 검증

2. **선택적 RIO 구현**
   - [ ] Windows 8+ 감지
   - [ ] RIO 함수 포인터 로드
   - [ ] 배치 처리 최적화

### 장기 (Week 5-8)

1. **Linux io_uring 구현**
   - [ ] IOUringAsyncIOProvider 구현
   - [ ] Ring Buffer 관리
   - [ ] Linux 통합 테스트

2. **성능 최적화**
   - [ ] 배치 크기 튜닝
   - [ ] 메모리 할당 최적화
   - [ ] 최종 벤치마크

---

## 📖 사용 가이드

### 1. 신규 개발자

```
1. 이 README 읽기
2. 02_Coding_Conventions_Guide 정독 (코드 이해 전)
3. 01_IOCP_Architecture_Analysis (기존 설계 이해)
4. 03_Implementation_Roadmap (전체 계획 파악)
```

### 2. 신규 기능 개발

```
1. 03_Implementation_Roadmap에서 해당 Phase 확인
2. 02_Coding_Conventions_Guide에서 규칙 재확인
3. 코드 구현 시 모든 라인에 주석 추가
4. enum 값은 반드시 설명 주석 포함
```

### 3. 코드 리뷰

```
체크리스트:
☐ Allman 스타일 준수
☐ 모든 라인에 주석 있음
☐ enum 값에 주석 있음
☐ 네이밍 컨벤션 준수
☐ 플랫폼 분기에 설명 주석
☐ std::unique_ptr 사용
☐ const 정확성 확인
☐ 예외 처리 확인
```

---

## 💡 주요 설계 결정 (Design Decisions)

### 1. Allman 스타일 선택 이유
- 한국 개발자에게 더 읽기 쉬움
- RAON Server Engine과 일관성
- 중괄호 위치 명확

### 2. 한글+영문 주석
- 팀 전체 이해도 향상
- 유지보수 용이
- 한국 회사의 표준

### 3. 플랫폼 추상화
- Windows IOCP의 우수한 성능 활용
- Linux epoll의 확장성
- macOS의 호환성

### 4. C++17/20 사용
- 더 안전한 메모리 관리 (std::unique_ptr)
- 현대적인 문법
- 유지보수 개선

---

## 📞 문의 및 피드백

**문서 관련 질문**:
- [ ] IOCP 개념 명확하지 않음
- [ ] 코딩 규칙 이의 있음
- [ ] 개발 일정 조정 필요

**문서 개선 사항**:
- [ ] 추가 설명 필요
- [ ] 예제 부족
- [ ] 오류 발견

---

## 📝 변경 이력 (Changelog)

| 날짜 | 버전 | 내용 |
|------|------|------|
| 2026-01-27 | 1.0 | 초기 문서 생성 (3개 파일: 01-03) |
| 2026-01-27 | 2.0 | Phase 2 마이그레이션 계획 추가 (4개 파일: 04-06) |
| TBD | 2.1 | 성능 분석 결과 추가 (07_Performance_Analysis.md) |
| TBD | 3.0 | 구현 시작 (소스 코드) |

---

## ✅ 완료 항목

- [x] RAON Server Engine IOCP 분석 (01)
- [x] 코딩 컨벤션 가이드 작성 (02)
- [x] 구현 로드맵 수립 (03)
- [x] 참고 파일 목록 작성 (04)
- [x] RIO/io_uring 마이그레이션 계획 (05)
- [x] 크로스 플랫폼 아키텍처 설계 (06)
- [x] 문서 인덱스 생성

## ⏳ 예정 항목

- [ ] 성능 분석 및 벤치마크 (07_Performance_Analysis.md)
- [ ] AsyncIOProvider 인터페이스 구현
- [ ] RIO 백엔드 구현
- [ ] io_uring 백엔드 구현
- [ ] 크로스 플랫폼 통합 테스트
- [ ] 프로덕션 배포

---

## 🔗 참고 링크

**Windows IOCP**:
- Microsoft Docs: Input/Output Completion Ports
- Winsock2 API Reference

**Linux epoll**:
- man-pages: epoll_create, epoll_ctl, epoll_wait
- Linux Kernel Documentation

**macOS kqueue**:
- FreeBSD Kernel Interfaces Manual
- Apple Developer Documentation

**C++ 표준**:
- C++ Core Guidelines
- cppreference.com

**참고 프로젝트**:
- RAON Server Engine (E:\Work\RAON\Server)
- Boost.Asio
- libuv

---

**마지막 수정**: 2026-01-27  
**작성자**: AI Assistant  
**상태**: 검토 대기

