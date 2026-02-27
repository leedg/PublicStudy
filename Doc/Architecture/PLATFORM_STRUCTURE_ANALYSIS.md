# Platform/Network 구조 분석 및 정리 계획

## 작성일: 2026-02-06 (2026-02-09 업데이트)

> ⚠️ **레거시 분석 문서**
> 이 문서는 리팩터링 과정의 구조 분석 기록입니다.
> `IOCPNetworkEngine.h/cpp`는 현재 코드에서 **이미 제거**되었습니다.
> 이 문서에 등장하는 파일/클래스 다수는 더 이상 존재하지 않습니다.
> 현재 구조는 `Doc/Architecture/NetworkArchitecture.md`를 참조하세요.

---

## 현재 구조 분석

### 1. NetworkEngine 계층 구조

```
INetworkEngine (인터페이스)
└── BaseNetworkEngine (Network/Core/)
    ├── WindowsNetworkEngine (Network/Platforms/)  ✅ 사용 중
    ├── LinuxNetworkEngine (Network/Platforms/)    ✅ 사용 중
    └── macOSNetworkEngine (Network/Platforms/)    ✅ 사용 중
```

### 2. AsyncIOProvider 위치

```
AsyncIOProvider (Network/Core/)
├── IocpAsyncIOProvider (Platforms/Windows/)  ✅ 구현 존재
├── RIOAsyncIOProvider (Platforms/Windows/)   ✅ 구현 존재
├── EpollAsyncIOProvider (Platforms/Linux/)   ✅ 구현 존재
├── IOUringAsyncIOProvider (Platforms/Linux/) ✅ 구현 존재
└── KqueueAsyncIOProvider (Platforms/macOS/)  ✅ 구현 존재
```

### 3. 파일 위치 문제

#### Network/Core/
- **BaseNetworkEngine.h/cpp** ✅ - 사용 중 (Platform 기본 클래스)
- **AsyncIOProvider.h/cpp** ✅ - 사용 중 (인터페이스)

#### Network/Platforms/
- **WindowsNetworkEngine.h/cpp** ✅ - 사용 중
- **LinuxNetworkEngine.h/cpp** ✅ - 사용 중
- **macOSNetworkEngine.h/cpp** ✅ - 사용 중

#### Platforms/Windows/
- **IocpAsyncIOProvider.h** ✅ - 헤더만 (구현 WindowsIOCPProvider.cpp)
- **WindowsIOCPProvider.cpp** ⚠️ - 파일명 불일치
- **RIOAsyncIOProvider.h** ✅ - 헤더만 (구현 WindowsRIOProvider.cpp)
- **WindowsRIOProvider.cpp** ⚠️ - 파일명 불일치

---

## 중복 및 혼동 사항

### 1. IOCPNetworkEngine vs WindowsNetworkEngine

**IOCPNetworkEngine (사용 안함)**:
- 위치: `Network/Core/IOCPNetworkEngine.h/cpp`
- 직접 INetworkEngine 구현
- Session 기반 IOCP 서버 엔진
- **현재 사용 안함** ❌

**WindowsNetworkEngine (사용 중)**:
- 위치: `Network/Platforms/WindowsNetworkEngine.h/cpp`
- BaseNetworkEngine 상속
- IOCP와 RIO 모두 지원
- **현재 사용 중** ✅

**결론**: IOCPNetworkEngine은 제거 대상

---

### 2. IocpAsyncIOProvider vs IOCPNetworkEngine

**목적이 다름**:

**IocpAsyncIOProvider**:
- AsyncIOProvider 인터페이스 구현
- 플랫폼 독립적 설계 (RIO/epoll/io_uring와 교체 가능)
- Session 독립적 I/O 추상화
- 멀티플랫폼 라이브러리용

**IOCPNetworkEngine**:
- 고수준 서버 엔진
- Session 관리 포함
- Windows 전용 서버 최적화
- **현재 사용 안함**

**결론**: 용도가 다르지만 IOCPNetworkEngine은 사용 안하므로 제거

---

### 3. 파일명 불일치

**현재**:
```
Platforms/Windows/IocpAsyncIOProvider.h      ✅
Platforms/Windows/WindowsIOCPProvider.cpp    ❌ 불일치

Platforms/Windows/RIOAsyncIOProvider.h       ✅
Platforms/Windows/WindowsRIOProvider.cpp     ❌ 불일치
```

**원하는 형태**:
```
Platforms/Windows/IocpAsyncIOProvider.h      ✅
Platforms/Windows/IocpAsyncIOProvider.cpp    ✅ 변경 필요

Platforms/Windows/RIOAsyncIOProvider.h       ✅
Platforms/Windows/RIOAsyncIOProvider.cpp     ✅ 변경 필요
```

---

## 정리 계획

### Phase 1: 파일명 변경 ✅

1. **WindowsIOCPProvider.cpp** → **IocpAsyncIOProvider.cpp**
2. **WindowsRIOProvider.cpp** → **RIOAsyncIOProvider.cpp**

### Phase 2: 사용 안하는 파일 제거 ✅

제거 대상:
- `Network/Core/IOCPNetworkEngine.h`
- `Network/Core/IOCPNetworkEngine.cpp`

이유:
- WindowsNetworkEngine으로 대체됨
- Factory에서 사용 안함 (CreateNetworkEngine → WindowsNetworkEngine 생성)
- TestServer, DBServer 모두 Factory 사용

### Phase 3: vcxproj 업데이트 ✅

- 파일명 변경 반영
- 제거된 파일 삭제
- 필터 업데이트

---

## 로직 중복 체크

### 1. WindowsNetworkEngine vs IOCPNetworkEngine

**WindowsNetworkEngine**:
```cpp
class WindowsNetworkEngine : public Core::BaseNetworkEngine
{
  public:
    enum class Mode { IOCP, RIO };
    explicit WindowsNetworkEngine(Mode mode = Mode::IOCP);

  protected:
    bool InitializePlatform() override;
    void ProcessCompletions() override;
};
```

**IOCPNetworkEngine**:
```cpp
class IOCPNetworkEngine : public INetworkEngine
{
  public:
    IOCPNetworkEngine();

    bool Initialize(size_t maxConnections, uint16_t port) override;
    void AcceptThread();
    void WorkerThread();
};
```

**차이점**:
- WindowsNetworkEngine은 BaseNetworkEngine의 템플릿 메서드 패턴 사용
- IOCPNetworkEngine은 모든 로직 직접 구현
- **기능적으로 중복** (IOCP 사용)

**결론**: IOCPNetworkEngine 제거해도 무방

---

### 2. AsyncIOProvider 구현체 간 중복

**Platform별 구현**:
- IocpAsyncIOProvider (Windows)
- RIOAsyncIOProvider (Windows)
- EpollAsyncIOProvider (Linux)
- IOUringAsyncIOProvider (Linux)
- KqueueAsyncIOProvider (macOS)

**중복 체크 결과**:
- 각각 다른 OS API 사용 (IOCP, RIO, epoll, io_uring, kqueue)
- 인터페이스 통일 (AsyncIOProvider)
- **중복 없음** ✅

---

## 최종 구조 (정리 후)

### 계층 구조
```
INetworkEngine (인터페이스)
└── BaseNetworkEngine (Network/Core/)
    ├── WindowsNetworkEngine (Network/Platforms/)
    ├── LinuxNetworkEngine (Network/Platforms/)
    └── macOSNetworkEngine (Network/Platforms/)
```

### AsyncIOProvider
```
AsyncIOProvider (Network/Core/)
├── IocpAsyncIOProvider (Platforms/Windows/)
├── RIOAsyncIOProvider (Platforms/Windows/)
├── EpollAsyncIOProvider (Platforms/Linux/)
├── IOUringAsyncIOProvider (Platforms/Linux/)
└── KqueueAsyncIOProvider (Platforms/macOS/)
```

### 파일명 규칙
```
Platforms/Windows/IocpAsyncIOProvider.h     ✅
Platforms/Windows/IocpAsyncIOProvider.cpp   ✅
Platforms/Windows/RIOAsyncIOProvider.h      ✅
Platforms/Windows/RIOAsyncIOProvider.cpp    ✅

Platforms/Linux/EpollAsyncIOProvider.h      ✅
Platforms/Linux/EpollAsyncIOProvider.cpp    ✅
Platforms/Linux/IOUringAsyncIOProvider.h    ✅
Platforms/Linux/IOUringAsyncIOProvider.cpp  ✅

Platforms/macOS/KqueueAsyncIOProvider.h     ✅
Platforms/macOS/KqueueAsyncIOProvider.cpp   ✅
```

---

## 변경 작업 순서

1. ✅ WindowsIOCPProvider.cpp → IocpAsyncIOProvider.cpp 이름 변경
2. ✅ WindowsRIOProvider.cpp → RIOAsyncIOProvider.cpp 이름 변경
3. ✅ vcxproj 파일 업데이트
4. ✅ IOCPNetworkEngine.h/cpp 제거
5. ✅ 빌드 테스트
6. ✅ 문서 작성
7. ✅ 커밋 & 푸시

---

## 영향 받는 파일

### 변경
- `Platforms/Windows/WindowsIOCPProvider.cpp` (이름 변경)
- `Platforms/Windows/WindowsRIOProvider.cpp` (이름 변경)
- `ServerEngine.vcxproj` (참조 업데이트)
- `ServerEngine.vcxproj.filters` (필터 업데이트)

### 제거
- `Network/Core/IOCPNetworkEngine.h`
- `Network/Core/IOCPNetworkEngine.cpp`

### 영향 없음
- `NetworkEngineFactory.cpp` (WindowsNetworkEngine만 사용)
- `TestServer.cpp` (Factory 사용)
- `DBServer.cpp` (Factory 사용)
- `Session.h` (friend class 선언 제거)
