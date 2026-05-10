# Visual Studio 필터 재구성

## 작성일: 2026-02-06

---

## 변경 개요

ServerEngine.vcxproj.filters의 필터 구조를 논리적으로 재구성하여 코드 네비게이션과 관리를 개선했습니다.

---

## 변경 전 구조

### 문제점

1. **Platform 필터 혼재**
   - `Network\Platforms\*NetworkEngine.*` 파일이 `Platform\*` 필터에 있음
   - `Platforms\*\*AsyncIOProvider.*` 파일이 같은 필터에 있음
   - 실제 파일 위치와 필터가 불일치

2. **Documentation 불완전**
   - Database 문서만 포함
   - 나머지 문서는 솔루션에만 존재

3. **Examples vs Tests**
   - 필터명은 `Examples`이지만 실제 폴더는 `Tests\Protocols`
   - 명칭 불일치로 혼란 발생

4. **중복된 Platform 정의**
   - Core\Network와 Platform\* 필터가 겹침
   - 네트워크 관련 파일이 두 곳에 분산

---

## 변경 후 구조

### 최상위 필터

```
Root
├── Core
│   └── Network
│       ├── Interfaces (NetworkEngine, AsyncIOProvider)
│       └── Platforms
│           ├── Windows (WindowsNetworkEngine, IocpProvider, RIOProvider)
│           ├── Linux (LinuxNetworkEngine, EpollProvider, IOUringProvider)
│           └── macOS (macOSNetworkEngine, KqueueProvider)
├── Interfaces (Database, Message)
├── Implementations
│   ├── Protocols (BaseMessageHandler)
│   └── Database (DatabaseFactory, ODBCDatabase, etc.)
├── Tests
│   └── Protocols (MessageHandler, PingPong, AsyncIOTest)
├── Utils (Logger, ThreadPool, SafeQueue, etc.)
└── Documentation
    ├── Architecture
    ├── Performance
    ├── Database
    ├── Network
    └── Development
```

---

## 주요 변경 사항

### 1. Core\Network 계층 구조화

**목적**: 모든 네트워크 관련 코드를 Core\Network 하위로 통합

```
Core\Network
├── Interfaces
│   ├── NetworkEngine.h (INetworkEngine 인터페이스)
│   └── AsyncIOProvider.h (AsyncIOProvider 인터페이스)
├── (루트)
│   ├── BaseNetworkEngine.h/cpp
│   ├── Session.h/cpp
│   ├── SessionManager.h/cpp
│   ├── NetworkEngineFactory.cpp
│   └── PlatformDetect.h/cpp
└── Platforms
    ├── Windows
    │   ├── WindowsNetworkEngine.h/cpp
    │   ├── IocpAsyncIOProvider.h/cpp
    │   └── RIOAsyncIOProvider.h/cpp
    ├── Linux
    │   ├── LinuxNetworkEngine.h/cpp
    │   ├── EpollAsyncIOProvider.h/cpp
    │   └── IOUringAsyncIOProvider.h/cpp
    └── macOS
        ├── macOSNetworkEngine.h/cpp
        └── KqueueAsyncIOProvider.h/cpp
```

**이점**:
- 플랫폼별 구현을 한눈에 파악 가능
- 인터페이스와 구현의 명확한 분리
- 파일 위치와 필터 일치

---

### 2. Examples → Tests 변경

**변경 전**: `Examples\Protocols`
**변경 후**: `Tests\Protocols`

**이유**:
- 실제 폴더명과 일치 (`Tests\Protocols\*`)
- 예제보다는 테스트 코드 성격
- `AsyncIOTest.cpp`도 Tests 필터로 통합

---

### 3. Documentation 필터 체계화

**추가된 하위 필터**:
- `Documentation\Architecture`: 아키텍처 관련 문서
- `Documentation\Performance`: 성능 최적화 문서
- `Documentation\Database`: 데이터베이스 문서
- `Documentation\Network`: 네트워크 관련 문서
- `Documentation\Development`: 개발 가이드, 리팩토링 계획

**현재 포함**:
- `Database\*.md` → `Documentation\Database`
- `REFACTORING_PLAN.md` → `Documentation\Development`

**향후 추가 예정**:
- `Doc\Architecture\*.md` → `Documentation\Architecture`
- `Doc\Performance\*.md` → `Documentation\Performance`
- `Doc\Development\*.md` → `Documentation\Development`

---

### 4. Implementations 정리

**유지**:
- `Implementations\Protocols`: BaseMessageHandler
- `Implementations\Database`: DatabaseFactory, ODBCDatabase, ConnectionPool

**추가**:
- `Database\ODBCDatabase_new.cpp` → `Implementations\Database`

---

## 파일 매핑 규칙

### Network 파일

| 실제 위치 | 필터 위치 | 설명 |
|----------|----------|------|
| `Network\Core\NetworkEngine.h` | `Core\Network\Interfaces` | INetworkEngine 인터페이스 |
| `Network\Core\AsyncIOProvider.h` | `Core\Network\Interfaces` | AsyncIOProvider 인터페이스 |
| `Network\Core\BaseNetworkEngine.*` | `Core\Network` | 기본 엔진 클래스 |
| `Network\Core\Session.*` | `Core\Network` | 세션 관리 |
| `Network\Platforms\Windows*.*` | `Core\Network\Platforms\Windows` | Windows 구현 |
| `Platforms\Windows\Iocp*.*` | `Core\Network\Platforms\Windows` | Windows IOCP 구현 |
| `Platforms\Windows\RIO*.*` | `Core\Network\Platforms\Windows` | Windows RIO 구현 |

### Database 파일

| 실제 위치 | 필터 위치 | 설명 |
|----------|----------|------|
| `Interfaces\IDatabase.h` | `Interfaces` | 데이터베이스 인터페이스 |
| `Database\DatabaseFactory.*` | `Implementations\Database` | 데이터베이스 팩토리 |
| `Database\ODBCDatabase.*` | `Implementations\Database` | ODBC 구현 |

---

## 이점

### 1. 명확한 코드 구조

- **인터페이스 vs 구현**: `Core\Network\Interfaces`와 `Core\Network\Platforms` 분리
- **플랫폼별 그룹화**: Windows, Linux, macOS 구현이 명확히 구분됨
- **일관성**: 파일 위치와 필터가 일치

### 2. 향상된 네비게이션

- 플랫폼별 구현 찾기 쉬움
- 인터페이스에서 구현으로 이동 용이
- 테스트 코드 빠른 접근

### 3. 확장성

- 새 플랫폼 추가 시: `Core\Network\Platforms\*` 하위에 추가
- 새 Provider 추가 시: 해당 플랫폼 폴더에 추가
- 문서 추가 시: `Documentation\*` 하위 적절한 위치에 추가

### 4. 팀 협업

- 필터 구조만으로 프로젝트 이해 가능
- 신규 개발자 온보딩 시간 단축
- 코드 리뷰 시 빠른 파일 탐색

---

## 솔루션 업데이트

### TestDBServer 추가

**프로젝트 정보**:
- 경로: `Server\DBServer\TestServer.vcxproj`
- GUID: `{C0003D60-E384-4C68-A4B5-0E0DA4D62A41}`
- 솔루션 폴더: `3.Server`

**목적**:
- DB 전용 서버 테스트
- DBTaskQueue 통합 테스트
- DBPingTimeManager 검증

---

## 미래 작업

### 1. 파일 위치 통일 (선택)

현재는 필터만 정리했지만, 향후 고려사항:

**옵션 A: 현재 구조 유지**
- `Network\Platforms\*` 유지
- `Platforms\*\*` 유지
- 필터만 `Core\Network\Platforms\*`로 표시

**옵션 B: 파일 위치 통일**
- 모든 Platform 파일을 `Network\Platforms\*\*`로 이동
- `Platforms\Windows\*` → `Network\Platforms\Windows\*`
- 필터와 실제 위치 완전 일치

**권장**: 옵션 A (현재 구조 유지)
- 이미 Provider는 `Platforms\*\*`에 위치
- NetworkEngine은 `Network\Platforms\*`에 위치
- 큰 리팩토링 없이 필터로 통합 표현 가능

---

### 2. Documentation 파일 추가

현재 vcxproj에는 Database 문서만 포함되어 있음. 향후 추가:

```xml
<None Include="..\..\Doc\Architecture\PLATFORM_STRUCTURE_ANALYSIS.md">
  <Filter>Documentation\Architecture</Filter>
</None>
<None Include="..\..\Doc\Performance\VALUE_COPY_OPTIMIZATION.md">
  <Filter>Documentation\Performance</Filter>
</None>
<None Include="..\..\Doc\Performance\LOCK_CONTENTION_ANALYSIS.md">
  <Filter>Documentation\Performance</Filter>
</None>
```

---

## 요약

### 완료된 작업
- ✅ TestDBServer 프로젝트를 솔루션에 추가
- ✅ Core\Network 계층 구조화
- ✅ Platform 필터 통합
- ✅ Tests 필터명 수정 (Examples → Tests)
- ✅ Documentation 하위 필터 추가

### 개선 효과
- **가독성**: 논리적인 필터 계층 구조
- **일관성**: 파일 위치와 필터 일치
- **확장성**: 새 플랫폼/Provider 추가 용이
- **유지보수**: 코드 탐색 시간 단축

### 영향 받은 파일
- `NetworkModuleTest.sln`: TestDBServer 추가
- `ServerEngine.vcxproj.filters`: 전체 재구성
