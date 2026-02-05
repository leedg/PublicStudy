# 구현 로드맵

**Version**: 2.0  
**Date**: 2026-01-27  
**Status**: Planning Phase with Platform Detection & Auto-Selection

---

## 0. 플랫폼별 네트워크 모듈 자동 선택 (Platform Detection & Auto-Selection)

### 0.1 개요 (Overview)

실행 파일이 **런타임에 플랫폼을 감지**하여 최적의 네트워크 모듈을 자동으로 선택합니다.

```
┌─────────────────────────────────────┐
│        실행 파일 시작                 │
└────────────────┬────────────────────┘
                 │
         ┌───────▼────────┐
         │ 플랫폼 감지      │
         └───────┬────────┘
                 │
    ┌────────────┼────────────┬──────────┐
    │            │            │          │
┌───▼──┐    ┌───▼──┐     ┌───▼──┐   ┌──▼───┐
│Windows│   │Linux │     │macOS │   │기타   │
└───┬──┘    └───┬──┘     └───┬──┘   └──┬───┘
    │            │            │         │
┌───▼─────────────────────────────────────────┐
│ 네트워크 모듈 선택 (순서대로 시도)          │
├─────────────────────────────────────────────┤
│ Windows:    RIO (8+) → IOCP → Fallback     │
│ Linux:      io_uring (5.1+) → epoll        │
│ macOS:      kqueue                        │
└─────────────────────────────────────────────┘
    │
┌───▼────────────────────────────┐
│ AsyncIOProvider 초기화           │
└───┬────────────────────────────┘
    │
┌───▼────────────────────────────┐
│ 애플리케이션 실행                │
└────────────────────────────────┘
```

### 0.2 플랫폼 감지 전략 (Platform Detection Strategy)

#### Windows 플랫폼

```cpp
// 1. 컴파일 타임 감지
#ifdef _WIN32
    #define PLATFORM_WINDOWS 1
#endif

// 2. 런타임 Windows 버전 감지
// 영문: Windows version detection using modern APIs
// 한글: 현대식 API를 사용한 Windows 버전 감지

// ✅ Recommended Method: VersionHelpers.h (Windows 7+)
#include <VersionHelpers.h>

// 영문: Check for Windows 8 or later
// 한글: Windows 8 이상 확인
if (IsWindows8OrGreater())
{
    // 영문: Can use RIO (Windows 8+)
    // 한글: RIO 사용 가능 (Windows 8+)
}

// ✅ Alternative Method: RtlGetVersion (more reliable)
#include <winternl.h>
#pragma comment(lib, "ntdll.lib")

typedef NTSTATUS(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

NTSTATUS GetWindowsVersion(DWORD& dwMajor, DWORD& dwMinor)
{
    HMODULE hModNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hModNtdll)
        return STATUS_DLL_NOT_FOUND;

    RtlGetVersionPtr fxGetVersion = (RtlGetVersionPtr)GetProcAddress(
        hModNtdll, "RtlGetVersion");
    
    if (!fxGetVersion)
        return STATUS_PROCEDURE_NOT_FOUND;

    RTL_OSVERSIONINFOW rovi = { 0 };
    rovi.dwOSVersionInfoSize = sizeof(rovi);
    
    NTSTATUS ntStatus = fxGetVersion(&rovi);
    if (NT_SUCCESS(ntStatus))
    {
        dwMajor = rovi.dwMajorVersion;
        dwMinor = rovi.dwMinorVersion;
        return STATUS_SUCCESS;
    }
    
    return ntStatus;
}

// 영문: Windows version mapping
// 한글: Windows 버전 매핑
// - Windows 8.1+ (6.3+): RIO 지원
// - Windows 8 (6.2): RIO 지원 (KB2600617 패치 필요)
// - Windows 7 (6.1): IOCP 만 지원
// - Windows Vista/XP: IOCP 지원 (구형)

// ❌ DEPRECATED: Do NOT use GetVersionEx()
// - Deprecated since Windows 8.1
// - Does not work correctly on Windows 11 without manifest
// - Will be removed in future Windows versions
// OSVERSIONINFO osvi = {};
// osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
// GetVersionEx(&osvi);  // ❌ Deprecated

// 3. RIO 지원 확인
HMODULE ws2_32 = GetModuleHandleA("ws2_32.dll");
if (ws2_32 && GetProcAddress(ws2_32, "RIOInitialize"))
{
    // 영문: RIO available
    // 한글: RIO 사용 가능
}
```

#### Linux 플랫폼

```cpp
// 1. 컴파일 타임 감지
#ifdef __linux__
    #define PLATFORM_LINUX 1
#endif

// 2. 커널 버전 감지
struct utsname buf;
uname(&buf);
// buf.release: "5.10.0", "4.19.0" 등
// 버전 문자열 파싱 → io_uring 지원 확인

// 3. io_uring 지원 확인 (런타임)
struct io_uring_params params = {};
struct io_uring ring;
int ret = io_uring_queue_init_params(256, &ring, &params);
if (ret == 0)
{
    // io_uring 사용 가능
    io_uring_queue_exit(&ring);
}
```

#### macOS 플랫폼

```cpp
// 1. 컴파일 타임 감지
#ifdef __APPLE__
    #define PLATFORM_MACOS 1
#endif

// 2. macOS 버전 감지
int mib[2] = {CTL_KERN, KERN_OSRELEASE};
char osrelease[256];
size_t len = sizeof(osrelease);
sysctl(mib, 2, osrelease, &len, nullptr, 0);
// osrelease: "20.6.0" (macOS 11.4) 등
```

### 0.3 네트워크 모듈 선택 로직 (Selection Logic)

#### Windows

```cpp
// File: Network/Platform/WindowsPlatformSelector.h

class WindowsPlatformSelector
{
public:
    static AsyncIOProvider* SelectProvider()
    {
        // 1. Windows 버전 확인
        if (IsWindows8OrLater())
        {
            // 2. RIO 사용 가능 여부 확인
            if (IsRIOSupported())
            {
                LOG_INFO("Selecting RIO (Registered I/O)");
                return new RIOAsyncIOProvider();
            }
        }
        
        // Fallback to IOCP
        LOG_INFO("Selecting IOCP (I/O Completion Ports)");
        return new IocpAsyncIOProvider();
    }

private:
    static bool IsWindows8OrLater()
    {
        // ✅ Use VersionHelpers.h (recommended)
        // 영文: Modern and reliable API for Windows version check
        // 한글: Windows 버전 확인을 위한 현대식 신뢰성 높은 API

        #include <VersionHelpers.h>
        
        // 영문: Returns true if Windows 8 or later
        // 한글: Windows 8 이상이면 true 반환
        return IsWindows8OrGreater();

        // ✅ Alternative: RtlGetVersion (if VersionHelpers not available)
        /*
        HMODULE hModNtdll = GetModuleHandleW(L"ntdll.dll");
        if (!hModNtdll)
            return false;

        typedef NTSTATUS(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
        RtlGetVersionPtr fxGetVersion = (RtlGetVersionPtr)GetProcAddress(
            hModNtdll, "RtlGetVersion");
        
        if (!fxGetVersion)
            return false;

        RTL_OSVERSIONINFEW rovi = { 0 };
        rovi.dwOSVersionInfoSize = sizeof(rovi);
        
        NTSTATUS ntStatus = fxGetVersion(&rovi);
        if (NT_SUCCESS(ntStatus))
        {
            // Windows 8: Major 6, Minor 2
            // Windows 8.1: Major 6, Minor 3
            // Windows 10+: Major 10+
            return (rovi.dwMajorVersion > 6) ||
                   (rovi.dwMajorVersion == 6 && rovi.dwMinorVersion >= 2);
        }

        return false;
        */

        // ❌ DO NOT USE THIS - Deprecated and unreliable:
        /*
        OSVERSIONINFO osvi = {};
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        GetVersionEx(&osvi);  // ❌ DEPRECATED - Windows 8.1+
        */
    }
    
    static bool IsRIOSupported()
    {
        // 영문: Check if RIO functions are available
        // 한글: RIO 함수의 사용 가능성 확인
        HMODULE ws2_32 = GetModuleHandleA("ws2_32.dll");
        if (!ws2_32) return false;
        
        auto pRIOInitialize = GetProcAddress(ws2_32, "RIOInitialize");
        return pRIOInitialize != nullptr;
    }
};
```

#### Linux

```cpp
// File: Network/Platform/LinuxPlatformSelector.h

class LinuxPlatformSelector
{
public:
    static AsyncIOProvider* SelectProvider()
    {
        // 1. 커널 버전 확인
        if (IsKernelVersion51OrLater())
        {
            // 2. io_uring 런타임 지원 확인
            if (IsIOUringSupported())
            {
                LOG_INFO("Selecting io_uring (Linux 5.1+)");
                return new IOUringAsyncIOProvider();
            }
        }
        
        // Fallback to epoll
        LOG_INFO("Selecting epoll (Linux 2.6+)");
        return new EpollAsyncIOProvider();
    }

private:
    static bool IsKernelVersion51OrLater()
    {
        struct utsname buf;
        if (uname(&buf) < 0) return false;
        
        // Parse buf.release: "5.10.0-1-generic"
        int major = 0, minor = 0;
        sscanf(buf.release, "%d.%d", &major, &minor);
        
        return (major > 5) || (major == 5 && minor >= 1);
    }
    
    static bool IsIOUringSupported()
    {
        struct io_uring ring;
        int ret = io_uring_queue_init(1, &ring, 0);
        if (ret >= 0)
        {
            io_uring_queue_exit(&ring);
            return true;
        }
        return false;
    }
};
```

#### macOS

```cpp
// File: Network/Platform/MacOSPlatformSelector.h

class MacOSPlatformSelector
{
public:
    static AsyncIOProvider* SelectProvider()
    {
        // macOS는 kqueue만 지원
        LOG_INFO("Selecting kqueue (macOS)");
        return new KqueueAsyncIOProvider();
    }
};
```

### 0.4 플랫폼 통합 팩토리 (Unified Factory)

```cpp
// File: Network/Platform/PlatformFactory.h

class PlatformFactory
{
public:
    // 1. 플랫폼 자동 감지 및 최적의 provider 선택
    static std::unique_ptr<AsyncIOProvider> CreateOptimalProvider()
    {
        #ifdef _WIN32
            return WindowsPlatformSelector::SelectProvider();
        #elif __linux__
            return LinuxPlatformSelector::SelectProvider();
        #elif __APPLE__
            return MacOSPlatformSelector::SelectProvider();
        #else
            LOG_ERROR("Unsupported platform");
            return nullptr;
        #endif
    }
    
    // 2. 명시적 플랫폼 선택 (테스트/디버깅용)
    static std::unique_ptr<AsyncIOProvider> CreateProvider(
        const char* platformHint)
    {
        if (!platformHint)
            return CreateOptimalProvider();
        
        std::string hint = platformHint;
        
        #ifdef _WIN32
            if (hint == "RIO")
                return std::make_unique<RIOAsyncIOProvider>();
            else if (hint == "IOCP")
                return std::make_unique<IocpAsyncIOProvider>();
        #elif __linux__
            if (hint == "io_uring")
                return std::make_unique<IOUringAsyncIOProvider>();
            else if (hint == "epoll")
                return std::make_unique<EpollAsyncIOProvider>();
        #elif __APPLE__
            if (hint == "kqueue")
                return std::make_unique<KqueueAsyncIOProvider>();
        #endif
        
        // Fallback
        return CreateOptimalProvider();
    }
    
    // 3. 현재 플랫폼 정보 조회
    static void PrintPlatformInfo()
    {
        #ifdef _WIN32
            OSVERSIONINFO osvi = {};
            osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
            GetVersionEx(&osvi);
            
            printf("Platform: Windows %d.%d\n",
                   osvi.dwMajorVersion, osvi.dwMinorVersion);
            
            if (WindowsPlatformSelector::IsRIOSupported())
                printf("  - RIO: Supported\n");
            else
                printf("  - IOCP: Only\n");
        #elif __linux__
            struct utsname buf;
            uname(&buf);
            printf("Platform: Linux (%s)\n", buf.release);
            
            if (LinuxPlatformSelector::IsIOUringSupported())
                printf("  - io_uring: Supported\n");
            else
                printf("  - epoll: Only\n");
        #elif __APPLE__
            printf("Platform: macOS (kqueue)\n");
        #endif
    }
};
```

### 0.5 애플리케이션 초기화 (Application Initialization)

```cpp
// File: Main.cpp

int main(int argc, char* argv[])
{
    // 1. 플랫폼 정보 출력
    printf("=== Network Module Auto-Selection ===\n");
    PlatformFactory::PrintPlatformInfo();
    printf("\n");
    
    // 2. 최적의 provider 선택 (또는 명시적 선택)
    const char* platformHint = nullptr;
    if (argc > 1)
    {
        platformHint = argv[1];  // 예: ./server RIO
    }
    
    auto provider = PlatformFactory::CreateProvider(platformHint);
    if (!provider)
    {
        fprintf(stderr, "Failed to create AsyncIOProvider\n");
        return 1;
    }
    
    printf("Selected Provider: %s\n", provider->GetInfo().name);
    printf("\n");
    
    // 3. IocpCore 초기화 (provider 사용)
    IocpCore iocp;
    if (!iocp.Initialize(provider.get(), 4096, 10000))
    {
        fprintf(stderr, "Failed to initialize IocpCore\n");
        return 1;
    }
    
    // 4. 서버 시작
    ServiceCoordinator coordinator;
    if (!coordinator.Initialize(&iocp, ServiceType::SERVER))
    {
        fprintf(stderr, "Failed to initialize ServiceCoordinator\n");
        return 1;
    }
    
    printf("Server started successfully!\n");
    printf("Listening on port 5000...\n");
    
    // 5. 메인 루프
    while (running)
    {
        iocp.HandleIocp();
        // 다른 처리...
    }
    
    return 0;
}
```

### 0.6 로깅 출력 예시

**Windows 10에서 RIO 지원:**
```
=== Network Module Auto-Selection ===
Platform: Windows 10.0
  - RIO: Supported

Selected Provider: RIO (Registered I/O)
Server started successfully!
Listening on port 5000...
```

**Linux 5.10에서 io_uring 지원:**
```
=== Network Module Auto-Selection ===
Platform: Linux (5.10.0-1-generic)
  - io_uring: Supported

Selected Provider: io_uring
Server started successfully!
Listening on port 5000...
```

**Windows 7에서 RIO 미지원 (IOCP Fallback):**
```
=== Network Module Auto-Selection ===
Platform: Windows 7.1
  - IOCP: Only

Selected Provider: IOCP (I/O Completion Ports)
Server started successfully!
Listening on port 5000...
```

**macOS에서 kqueue:**
```
=== Network Module Auto-Selection ===
Platform: macOS (kqueue)

Selected Provider: kqueue
Server started successfully!
Listening on port 5000...
```

### 0.7 환경 변수를 통한 선택 (Optional)

```cpp
// 환경 변수로 provider 강제 지정 (디버깅용)
const char* forcedProvider = getenv("NETWORK_PROVIDER");
auto provider = forcedProvider ? 
    PlatformFactory::CreateProvider(forcedProvider) :
    PlatformFactory::CreateOptimalProvider();

// 사용 예:
// Windows: set NETWORK_PROVIDER=IOCP
// Linux:   export NETWORK_PROVIDER=epoll
```

### 0.8 구현 일정

**Week 1 (Phase 1에 포함)**:
- [ ] PlatformFactory 클래스 설계
- [ ] WindowsPlatformSelector 구현
- [ ] LinuxPlatformSelector 구현
- [ ] MacOSPlatformSelector 구현
- [ ] 플랫폼 감지 테스트

**Week 2-3 (Phase 2-3)**:
- [ ] IocpCore에 provider 통합
- [ ] 각 플랫폼별 테스트
- [ ] 로깅 및 디버깅 기능 추가



```
E:\MyGitHub\PublicStudy\NetworkModuleTest\
├── Doc/
│   ├── 01_IOCP_Architecture_Analysis.md           ✓ (완성)
│   ├── 02_Coding_Conventions_Guide.md             ✓ (완성)
│   ├── 03_Implementation_Roadmap.md               (현재 파일)
│   ├── 04_API_Design_Document.md                 (예정)
│   └── 05_Performance_Guidelines.md              (예정)
│
├── Server/
│   └── ServerEngine/
│       ├── Core/                                  # 핵심 모듈
│       │   ├── Network/
│       │   │   ├── Iocp/                         # Windows IOCP
│       │   │   │   ├── IocpCore.h/cpp
│       │   │   │   ├── IocpObject.h
│       │   │   │   ├── IocpObjectListener.h/cpp
│       │   │   │   └── IocpObjectJob.h/cpp
│       │   │   ├── Epoll/                        # Linux epoll
│       │   │   │   ├── EpollCore.h/cpp
│       │   │   │   └── EpollObject.h
│       │   │   ├── Kqueue/                       # macOS kqueue
│       │   │   │   ├── KqueueCore.h/cpp
│       │   │   │   └── KqueueObject.h
│       │   │   ├── AsyncIOProvider.h             # 추상 인터페이스
│       │   │   ├── Session/
│       │   │   │   ├── IocpObjectSession.h/cpp
│       │   │   │   ├── SessionPool.h/cpp
│       │   │   │   └── CryptoSession.h/cpp
│       │   │   ├── Buffer/
│       │   │   │   ├── SendBufferChunkPool.h/cpp
│       │   │   │   └── SendBufferHelper.h/cpp
│       │   │   ├── ServiceCoordinator.h/cpp
│       │   │   └── NetworkTypeDef.h/cpp
│       │   ├── Memory/                           # 메모리 관리
│       │   ├── Lock/                             # 동기화
│       │   ├── Log/                              # 로깅
│       │   └── Utils/
│       │
│       ├── Sample/                               # 샘플 코드
│       │   ├── EchoServer.h/cpp
│       │   ├── EchoClient.h/cpp
│       │   └── LoadTestServer.h/cpp
│       │
│       └── CMakeLists.txt
│
├── Client/
│   └── (기존 클라이언트)
│
└── NetworkModuleTest.sln
```

---

## 2. 개발 단계 (Development Phases)

### Phase 1: 기본 구조 (Weeks 1-2)

#### 1.1 프로젝트 설정
- [ ] CMake 설정 (Windows/Linux/macOS 크로스 플랫폼)
- [ ] 헤더 파일 인클루드 경로 설정
- [ ] 플랫폼별 컴파일러 플래그 설정
- [ ] 빌드 출력 디렉토리 구성

#### 1.2 핵심 타입 정의
- [ ] AsyncIOProvider 추상 인터페이스
- [ ] OverlappedEx 구조체 (Windows)
- [ ] EventData 구조체 (Linux/macOS)
- [ ] IocpObject 인터페이스
- [ ] ServiceType 열거형

#### 1.3 메모리 관리 기반
- [ ] MemoryPool 구현
- [ ] SendBuffer/RecvBuffer 관리
- [ ] RefCount 기반 세션 생명주기

#### 1.4 플랫폼 감지 및 네트워크 모듈 선택 (★ 새로 추가)
**목표**: 실행 파일이 런타임에 플랫폼을 감지하여 최적의 네트워크 모듈 자동 선택

##### 1.4.1 플랫폼 감지 구현
- [ ] PlatformDetector 유틸 클래스 구현
  - Windows 버전 감지 (GetVersionEx)
  - Linux 커널 버전 감지 (uname)
  - macOS 버전 감지 (sysctl)
  
- [ ] RIO 지원 확인 (Windows 8+)
  - ws2_32.dll에서 RIOInitialize 함수 확인
  - RuntimeCheck 기능 구현
  
- [ ] io_uring 지원 확인 (Linux 5.1+)
  - io_uring_queue_init 런타임 테스트
  - Fallback to epoll 메커니즘

##### 1.4.2 플랫폼 선택 팩토리 구현
- [ ] PlatformFactory 클래스 설계
  ```cpp
  class PlatformFactory
  {
  public:
      // 1. 자동 최적 provider 선택
      static std::unique_ptr<AsyncIOProvider> CreateOptimalProvider();
      
      // 2. 명시적 provider 선택 (테스트용)
      static std::unique_ptr<AsyncIOProvider> CreateProvider(
          const char* platformHint);
      
      // 3. 플랫폼 정보 출력
      static void PrintPlatformInfo();
  };
  ```

- [ ] 플랫폼별 Selector 클래스
  - WindowsPlatformSelector (RIO → IOCP)
  - LinuxPlatformSelector (io_uring → epoll)
  - MacOSPlatformSelector (kqueue)

##### 1.4.3 테스트 및 로깅
- [ ] PlatformDetectionTest.cpp 작성
  ```cpp
  TEST(PlatformDetection, WindowsRIOSupport)
  TEST(PlatformDetection, LinuxIOUringSupport)
  TEST(PlatformDetection, FallbackMechanisms)
  ```

- [ ] 플랫폼 정보 로깅
  ```
  === Network Module Auto-Selection ===
  Platform: Windows 10.0
    - RIO: Supported
  Selected Provider: RIO (Registered I/O)
  ```

##### 1.4.4 애플리케이션 통합
- [ ] Main.cpp 업데이트
  ```cpp
  // 1. 플랫폼 정보 출력
  PlatformFactory::PrintPlatformInfo();
  
  // 2. Provider 자동 선택
  auto provider = PlatformFactory::CreateOptimalProvider();
  
  // 3. IocpCore 초기화
  IocpCore iocp;
  iocp.Initialize(provider.get(), 4096, 10000);
  ```

- [ ] 환경 변수 지원 (NETWORK_PROVIDER)
  ```bash
  # Windows: 강제로 IOCP 사용
  set NETWORK_PROVIDER=IOCP
  
  # Linux: 강제로 epoll 사용
  export NETWORK_PROVIDER=epoll
  ```

##### 1.4.5 구현 체크리스트
- [ ] PlatformDetector 클래스 (100줄)
- [ ] WindowsPlatformSelector 클래스 (80줄)
- [ ] LinuxPlatformSelector 클래스 (100줄)
- [ ] MacOSPlatformSelector 클래스 (60줄)
- [ ] PlatformFactory 클래스 (150줄)
- [ ] 단위 테스트 (200줄)
- [ ] 통합 테스트 (100줄)

**예상 시간**: 16시간 (Phase 1의 30%)



### Phase 2: Windows 네트워크 모듈 (Weeks 3-4)

#### 2.1 Windows 모듈 선택 및 IocpCore 구현
- [ ] IocpAsyncIOProvider 구현 (IOCP 호환성)
- [ ] RIOAsyncIOProvider 구현 (선택, Windows 8+)
- [ ] IocpCore 업데이트 (AsyncIOProvider 통합)
  ```cpp
  class IocpCore
  {
  private:
      std::unique_ptr<AsyncIOProvider> mAsyncProvider;  // ★ 추가
      
  public:
      bool Initialize(AsyncIOProvider* provider, ...)
      {
          mAsyncProvider = provider;
          // ... 초기화
      }
  };
  ```

#### 2.2 IocpObjectSession 호환성
- [ ] SendData / RecvData 메서드 업데이트
  ```cpp
  // Before: WSASend 직접 호출
  // After: mAsyncProvider->SendAsync 호출
  ```
- [ ] RefCount 기반 라이프사이클 유지
- [ ] 버퍼 관리 (Send/Recv) 호환성 확인

#### 2.3 서버 리스너 통합
- [ ] IocpObjectListener 업데이트
- [ ] ServiceCoordinator와 통합
- [ ] SessionPool 구현

#### 2.4 Windows EchoServer 샘플
- [ ] 서버 초기화 (플랫폼 자동 선택 적용)
  ```cpp
  auto provider = PlatformFactory::CreateOptimalProvider();
  printf("Selected: %s\n", provider->GetInfo().name);
  ```
- [ ] 클라이언트 연결 수락
- [ ] 데이터 수신 및 에코 송신
- [ ] 정상 종료 처리

#### 2.5 Windows 플랫폼별 테스트
- [ ] Windows 10 with RIO 테스트
- [ ] Windows 7 with IOCP 테스트 (Fallback 확인)
- [ ] 성능 벤치마크

**예상 시간**: 60시간 (Week 3-4)



### Phase 3: Linux epoll (Weeks 5-6)

#### 3.1 EpollCore 구현
- [ ] epoll_create / epoll_ctl / epoll_wait
- [ ] 이벤트 마스크 설정 (EPOLLIN, EPOLLOUT)
- [ ] 오류 처리

#### 3.2 Linux Session 적응
- [ ] 추상 인터페이스 유지
- [ ] 플랫폼별 Send/Recv 구현
- [ ] 논블로킹 소켓 처리
- [ ] 에러 코드 맵핑

#### 3.3 크로스 플랫폼 테스트
- [ ] Linux 샘플 빌드
- [ ] EchoServer Linux 버전
- [ ] 상호 연결 테스트 (Windows ↔ Linux)

### Phase 4: macOS kqueue (Week 7)

#### 4.1 KqueueCore 구현
- [ ] kqueue() / kevent()
- [ ] 이벤트 필터 설정 (EVFILT_READ, EVFILT_WRITE)
- [ ] 신호 처리

#### 4.2 macOS 특화
- [ ] BSD 소켓 호환성
- [ ] macOS 에러 처리
- [ ] 성능 최적화

### Phase 5: 고급 기능 (Weeks 8-10)

#### 5.1 RIO (Registered I/O) 지원 (Windows 8.1+)
- [ ] RIO API 래핑
- [ ] IOCP 기반 완료 포트
- [ ] 성능 벤치마크

#### 5.2 암호화 및 보안
- [ ] CryptoSession 구현
- [ ] TLS/SSL 통합
- [ ] 해시 함수 (MD5, SHA1)

#### 5.3 로드 밸런싱
- [ ] 멀티 워커 스레드
- [ ] JobSerializer
- [ ] 부하 분산 알고리즘

#### 5.4 모니터링 및 통계
- [ ] 성능 카운터
- [ ] 연결 통계
- [ ] 버퍼 사용률 모니터링

### Phase 6: 문서화 및 테스트 (Weeks 11-12)

#### 6.1 API 문서
- [ ] Doxygen 문서화
- [ ] 사용 예시
- [ ] 성능 튜닝 가이드

#### 6.2 단위 테스트
- [ ] 세션 라이프사이클 테스트
- [ ] 동시성 테스트
- [ ] 에러 처리 테스트

#### 6.3 통합 테스트
- [ ] 크로스 플랫폼 호환성
- [ ] 부하 테스트 (concurrent connections)
- [ ] 장시간 안정성 테스트

---

## 3. 주요 클래스 설계

### 3.1 AsyncIOProvider (추상)

```cpp
// 모든 플랫폼에 대한 일관된 인터페이스
class AsyncIOProvider
{
public:
    virtual bool Initialize(int maxConcurrentThreads) = 0;
    virtual void Shutdown() = 0;
    
    virtual bool RegisterHandle(void* handle) = 0;
    virtual void WaitForCompletion(int timeoutMs) = 0;
    virtual void PostCompletion(Job* job) = 0;
    
    virtual ServiceCoordinator* RegisterServerService(
        uint16 port, int serviceCount, SessionPool* pool) = 0;
    virtual ServiceCoordinator* RegisterClientService(
        const std::string& address, uint16 port, 
        int serviceCount, SessionPool* pool) = 0;
    
    virtual ~AsyncIOProvider() = default;
};
```

### 3.2 Session 생명주기

```
┌─────────────────────────────────────────┐
│         Session Lifecycle                │
├─────────────────────────────────────────┤
│                                          │
│  [Pooled] → [Acquired]                   │
│     ↓         ↓                          │
│   Reset    Connect()/Listen()           │
│              ↓                          │
│           [Connected]                   │
│             ↓    ↓                      │
│         Recv()  Send()                  │
│             ↓    ↓                      │
│           [Active]                      │
│             ↓                          │
│         Disconnect()                    │
│             ↓                          │
│          [Closed]                       │
│             ↓                          │
│         Reset()                         │
│             ↓                          │
│          [Pooled]                       │
│                                          │
└─────────────────────────────────────────┘
```

### 3.3 SendBuffer 흐름

```
User App
    ↓
Send(buffer, len)
    ↓
─────────────────────
│ ReserveSend()      │ (즉시 송신 가능하지 않으면)
└─────────────────────
    ↓
mSendPendingQueue (ThreadSafeQueue)
    ↓
FlushSend()
    ↓
FillSendBuffer() (여러 버퍼 병합)
    ↓
WSASend (IOCP overlapped)
    ↓
HandleIocpSend()
    ↓
OnSend() (사용자 콜백)
```

---

## 4. 플랫폼별 전략

### 4.1 Windows (IOCP)

| 기능 | 구현 |
|------|------|
| 비동기 I/O | CreateIoCompletionPort |
| 서버 연결 수락 | AcceptEx |
| 클라이언트 연결 | ConnectEx |
| 데이터 송수신 | WSASend / WSARecv |
| 연결 종료 | DisconnectEx |
| Job 큐 | PostQueuedCompletionStatus |

**장점**:
- 높은 성능 (커널 최적화)
- 우수한 에러 처리
- 연결 당 스레드 모델 필요 없음

**주의사항**:
- Vista 이상 필수
- Winsock 초기화 필수
- 완료 포트 종료 신호 처리

### 4.2 Linux (epoll)

| 기능 | 구현 |
|------|------|
| 비동기 I/O | epoll_create / epoll_wait |
| 이벤트 등록 | epoll_ctl (ADD, MOD, DEL) |
| 마스크 | EPOLLIN, EPOLLOUT, EPOLLERR |
| 논블로킹 소켓 | fcntl (O_NONBLOCK) |
| 데이터 송수신 | send / recv |

**장점**:
- 높은 확장성 (millions of connections)
- Linux 네이티브

**주의사항**:
- 논블로킹 소켓 필수
- Level-triggered vs Edge-triggered 모드
- 에러 처리: EAGAIN, EWOULDBLOCK

### 4.3 macOS (kqueue)

| 기능 | 구현 |
|------|------|
| 비동기 I/O | kqueue() / kevent() |
| 필터 | EVFILT_READ, EVFILT_WRITE |
| 플래그 | EV_ADD, EV_DELETE, EV_ENABLE |
| 데이터 송수신 | send / recv |

**장점**:
- BSD/macOS 통합
- 매우 유연한 필터 시스템

**주의사항**:
- 리소스 제한 확인 (ulimit)
- 논블로킹 소켓 필수

---

## 5. 성능 최적화 전략

### 5.1 버퍼 최적화

```cpp
// 권장: SendBuffer 병합
std::vector<SendBufferEntity*> pendingBuffers;
DWORD totalSize = 0;
WSABUF wsaBufs[MAX_WSABUF_COUNT];

int count = 0;
for (auto* buffer : pendingBuffers)
{
    wsaBufs[count].buf = buffer->GetData();
    wsaBufs[count].len = buffer->GetSize();
    totalSize += buffer->GetSize();
    count++;
    if (count >= MAX_WSABUF_COUNT) break;
}

WSASend(socket, wsaBufs, count, nullptr, 0, overlapped, nullptr);
```

### 5.2 메모리 풀

```cpp
// SessionPool: 세션 재사용
class SessionPool
{
    std::vector<std::unique_ptr<Session>> mAllSessions;
    std::queue<Session*> mAvailableSessions;
};

// SendBufferPool: 버퍼 재사용
class SendBufferPool
{
    std::queue<std::unique_ptr<SendBuffer>> mAvailableBuffers;
};
```

### 5.3 멀티 워커 스레드

```cpp
// IOCP: 동시 처리 스레드 수 최적화
int workerThreadCount = std::thread::hardware_concurrency();
// CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, workerThreadCount);

// epoll: 스레드 풀
ThreadPool workerPool(workerThreadCount);
for (int i = 0; i < workerThreadCount; ++i)
{
    workerPool.Enqueue([this] { HandleEpollEvents(); });
}
```

---

## 6. 테스트 계획

### 6.1 단위 테스트

```cpp
// Session 라이프사이클
TEST(SessionTest, LifecycleTest)
{
    // Acquire → Connect → Send/Recv → Disconnect → Release
}

// RefCount 관리
TEST(RefCountTest, AtomicOperations)
{
    Session session;
    int ref1 = session.AddRef();
    int ref2 = session.ReleaseRef();
    EXPECT_EQ(ref1 - 1, ref2);
}

// SendBuffer 병합
TEST(SendBufferTest, Concatenation)
{
    // 여러 버퍼 → 하나의 WSASend 호출
}
```

### 6.2 통합 테스트

```cpp
// EchoServer ↔ EchoClient
- 텍스트 메시지 송수신
- 대용량 파일 전송
- 동시 다중 연결

// 크로스 플랫폼
- Windows Server ↔ Linux Client
- macOS Server ↔ Windows Client
- Linux Server ↔ Linux Client
```

### 6.3 성능 테스트

| 메트릭 | 목표 | 측정 |
|--------|------|------|
| Throughput | > 1Gbps | bytes/sec |
| Latency | < 1ms | RTT |
| Connections | > 100K | concurrent |
| CPU Usage | < 50% | idle |

---

## 7. 위험 요소 및 대응 (Risk Management)

| 위험 | 영향 | 대응 |
|------|------|------|
| 크로스 플랫폼 호환성 | 높음 | 조기 통합 테스트 |
| 메모리 누수 | 높음 | 스마트 포인터, Valgrind |
| 동시성 버그 | 높음 | Thread Sanitizer |
| 성능 저하 | 중간 | 프로파일링, 벤치마크 |
| API 불안정 | 중간 | 초기 설계 검토 |

---

## 8. 참고 자료

- RAON Server Engine 소스 코드
- Windows IOCP 공식 문서
- Linux man-pages (epoll, select, poll)
- macOS kqueue 문서
- Boost.Asio (크로스 플랫폼 참고)
- libuv (이벤트 루프 참고)

---

## 9. 다음 단계

1. **API 설계** (`04_API_Design_Document.md`)
   - 공개 인터페이스 정의
   - 사용자 콜백 인터페이스
   - 설정 옵션

2. **성능 가이드라인** (`05_Performance_Guidelines.md`)
   - 최적화 팁
   - 벤치마크 결과
   - 튜닝 파라미터

3. **구현 시작**
   - Phase 1: 프로젝트 구조 설정
   - Phase 2: Windows IOCP 구현
   - Phase 3+: 플랫폼 확장

---

## 플랫폼 감지 현대화 (Platform Detection Modernization)

### 개요

기존 플랫폼 감지 메커니즘을 현대식 API로 업그레이드하여 유지보수성 및 확장성을 향상시킵니다.

### Windows 플랫폼 감지 개선

#### ✅ Recommended: VersionHelpers.h (Windows 7+)

```cpp
#include <VersionHelpers.h>

// 영문: Modern version checking
// 한글: 현대식 버전 검사

bool IsModernWindowsAvailable()
{
    return IsWindows8OrGreater();
}

bool SupportRIO()
{
    return IsWindows8OrGreater();
}

bool SupportSockaddr_Storage()
{
    return IsWindowsVista OrGreater();
}
```

**장점**:
- 간결하고 읽기 쉬운 API
- Microsoft 공식 권장
- Visual Studio에 기본 제공

**단점**:
- Windows 7 이상만 지원
- 세부 버전 정보 미제공

#### ✅ Alternative: RtlGetVersion (더 신뢰성 있음)

```cpp
#include <winternl.h>
#pragma comment(lib, "ntdll.lib")

typedef NTSTATUS(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

struct WindowsVersionInfo
{
    DWORD major;
    DWORD minor;
    DWORD build;
};

bool GetWindowsVersion(WindowsVersionInfo& info)
{
    // 영문: Load ntdll dynamically
    // 한글: ntdll 동적 로드
    
    HMODULE hModNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hModNtdll)
    {
        LOG_ERROR("Failed to load ntdll.dll");
        return false;
    }
    
    // 영문: Get RtlGetVersion function
    // 한글: RtlGetVersion 함수 획득
    
    RtlGetVersionPtr fxGetVersion = (RtlGetVersionPtr)GetProcAddress(
        hModNtdll, "RtlGetVersion");
    
    if (!fxGetVersion)
    {
        LOG_ERROR("Failed to get RtlGetVersion");
        return false;
    }
    
    // 영문: Call and extract version info
    // 한글: 호출 및 버전 정보 추출
    
    RTL_OSVERSIONINFOW rovi = { 0 };
    rovi.dwOSVersionInfoSize = sizeof(rovi);
    
    NTSTATUS ntStatus = fxGetVersion(&rovi);
    if (NT_SUCCESS(ntStatus))
    {
        info.major = rovi.dwMajorVersion;
        info.minor = rovi.dwMinorVersion;
        info.build = rovi.dwBuildNumber;
        return true;
    }
    
    return false;
}
```

**장점**:
- 세부 버전 정보 제공
- 모든 Windows 버전에서 작동
- 더 신뢰할 수 있음

**단점**:
- 내부 API 사용
- 코드가 더 복잡

### Linux 플랫폼 감지

```cpp
#include <sys/utsname.h>
#include <sys/syscall.h>
#include <linux/version.h>

// 영문: Get kernel version
// 한글: 커널 버전 획득

struct KernelVersion
{
    int major;
    int minor;
    int patch;
};

bool GetKernelVersion(KernelVersion& version)
{
    struct utsname buf;
    if (uname(&buf) != 0)
    {
        LOG_ERROR("uname failed");
        return false;
    }
    
    // Parse version string (e.g., "5.15.0-56-generic")
    int parsed = sscanf(buf.release, "%d.%d.%d",
        &version.major, &version.minor, &version.patch);
    
    return parsed == 3;
}

// 영문: Check io_uring support
// 한글: io_uring 지원 확인

bool SupportsIOUring()
{
    KernelVersion version;
    if (!GetKernelVersion(version))
        return false;
    
    // io_uring: Linux 5.1+ (kernel 5.1.0)
    if (version.major > 5)
        return true;
    
    if (version.major == 5 && version.minor >= 1)
        return true;
    
    return false;
}

// 영문: Check epoll support (all modern Linux)
// 한글: epoll 지원 (모든 현대식 Linux)

bool SupportsEpoll()
{
    // epoll: Linux 2.5.45+ (always available on modern systems)
    return true;
}

// 영문: Runtime check using syscall
// 한글: 시스콜을 사용한 런타임 확인

bool CheckIOUringAvailable()
{
    // Create io_uring and check if supported
    struct io_uring ring;
    int ret = io_uring_queue_init(16, &ring, 0);
    
    if (ret < 0)
    {
        // io_uring not available
        return false;
    }
    
    io_uring_queue_exit(&ring);
    return true;
}
```

### macOS 플랫폼 감지

```cpp
#include <sys/types.h>
#include <sys/sysctl.h>
#include <AvailabilityMacros.h>

// 영문: Get macOS version
// 한글: macOS 버전 획득

struct MacOSVersion
{
    int major;
    int minor;
    int patch;
};

bool GetMacOSVersion(MacOSVersion& version)
{
    int mib[2] = { CTL_KERN, KERN_OSRELEASE };
    char release[256] = { 0 };
    size_t len = sizeof(release);
    
    if (sysctl(mib, 2, release, &len, nullptr, 0) != 0)
    {
        LOG_ERROR("sysctl failed");
        return false;
    }
    
    // Parse kernel release to macOS version
    // Kernel 20.x.x = macOS 11 (Big Sur)
    // Kernel 21.x.x = macOS 12 (Monterey)
    // etc.
    
    int kernel_major;
    sscanf(release, "%d", &kernel_major);
    version.major = kernel_major - 9;  // Approximate conversion
    
    return true;
}

// 영문: Check kqueue support (all macOS)
// 한글: kqueue 지원 (모든 macOS)

bool SupportsKqueue()
{
    // kqueue: macOS 10.0+ (always available)
    return true;
}

// 영문: Check Mach ports (for event notification)
// 한글: Mach 포트 확인 (이벤트 알림용)

bool SupportsMachPorts()
{
    // macOS 10.0+
    return true;
}
```

### 통합 플랫폼 감지

```cpp
// 파일: PlatformDetect.h

class PlatformDetector
{
public:
    enum class Platform
    {
        Windows,
        Linux,
        macOS,
        Unknown
    };
    
    enum class IOProvider
    {
        IOCP,      // Windows fallback
        RIO,       // Windows 8+
        IOUring,   // Linux 5.1+
        Epoll,     // Linux fallback
        Kqueue,    // macOS
        Unknown
    };
    
    // 영문: Detect current platform
    // 한글: 현재 플랫폼 감지
    
    static Platform DetectPlatform()
    {
#ifdef _WIN32
        return Platform::Windows;
#elif __linux__
        return Platform::Linux;
#elif __APPLE__
        return Platform::macOS;
#else
        return Platform::Unknown;
#endif
    }
    
    // 영문: Detect best IO provider
    // 한글: 최적 IO 제공자 감지
    
    static IOProvider DetectBestIOProvider()
    {
        Platform platform = DetectPlatform();
        
        switch (platform)
        {
            case Platform::Windows:
                if (IsWindows8OrGreater())
                    return IOProvider::RIO;
                return IOProvider::IOCP;
                
            case Platform::Linux:
                if (CheckIOUringAvailable())
                    return IOProvider::IOUring;
                return IOProvider::Epoll;
                
            case Platform::macOS:
                return IOProvider::Kqueue;
                
            default:
                return IOProvider::Unknown;
        }
    }
};
```

### 구현 체크리스트

- [ ] VersionHelpers.h 기반 Windows 감지
- [ ] RtlGetVersion 대체 구현
- [ ] Linux uname 기반 커널 버전 감지
- [ ] io_uring 런타임 가용성 확인
- [ ] macOS sysctl 버전 감지
- [ ] 통합 PlatformDetector 클래스
- [ ] 테스트 (모든 플랫폼 감지 경로)

