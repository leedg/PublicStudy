# 코딩 컨벤션 가이드

**Version**: 1.0  
**Date**: 2026-01-27  
**Target**: IOCP-based Network Library  
**Language**: C++ (C++17/20)  
**Platforms**: Windows (IOCP), Linux (epoll), macOS (kqueue)

---

## 1. 브래이스 스타일 (Brace Style)

### 1.1 Allman 스타일 (필수)

**모든 컨트롤 구조와 함수에 Allman 스타일 적용**

```cpp
// ✓ 올바른 예시
if (condition)
{
    DoSomething();
    DoAnother();
}
else
{
    HandleError();
}

// ✗ 잘못된 예시 (1TBS)
if (condition) {
    DoSomething();
}

// ✓ 함수 정의
void MyFunction()
{
    // 함수 본문
}

// ✓ 클래스 정의
class MyClass
{
private:
    int mValue;

public:
    MyClass() { }
    ~MyClass() { }
};

// ✓ while/for 루프
while (condition)
{
    Process();
}

for (int i = 0; i < count; ++i)
{
    Handle(i);
}

// ✓ switch 문
switch (value)
{
case 1:
    HandleOne();
    break;
case 2:
    HandleTwo();
    break;
default:
    HandleDefault();
    break;
}
```

### 1.2 단일 라인 제외

```cpp
// ✓ 단일 명령문은 함께 작성 가능
if (ptr == nullptr) return false;

// ✓ 그래도 명확하면 Allman 사용
if (ptr == nullptr)
{
    return false;
}

// ✗ 절대 금지: 괄호 없이 여러 라인
if (condition)
    statement1;
    statement2;  // 위험: statement2는 항상 실행됨
```

---

## 2. 들여쓰기 및 공백

### 2.1 들여쓰기

```cpp
// ✓ 4칸 스페이스 (탭 금지)
void MyFunction()
{
    if (condition)
    {
        for (int i = 0; i < 10; ++i)
        {
            DoSomething(i);
        }
    }
}

// ✗ 탭 사용 금지
// ✗ 2칸 들여쓰기 금지
```

### 2.2 라인 길이

```cpp
// ✓ 권장: 120 자 이하
int result = CallFunction(param1, param2, param3);

// 길 때는 나눔
int result = CallVeryLongFunctionName(
    param1, 
    param2, 
    param3, 
    param4
);

// ✗ 피하기: 80자 초과는 주의
// int result = CallFunction(param1, param2, param3, param4, param5, param6, param7);
```

---

## 3. 주석 규칙 (Comment Rules)

### 3.1 의무 주석 (Mandatory Comments)

**모든 코드 라인에 설명 주석 추가**

```cpp
// ✓ 함수 선언 전 설명
// 클라이언트 세션에서 데이터를 송신하고 완료를 기다림
// Send data from client session and wait for completion
bool SendData(const uchar* buffer, int length);

// ✓ 복잡한 로직에 주석
// IOCP 핸들을 생성: 완료 포트를 설정하고 시작 대기
// Create IOCP handle: set completion port and initialize startup
mCompletionPort = CreateIoCompletionPort(
    INVALID_HANDLE_VALUE,   // 새 IOCP 생성 / Create new IOCP
    nullptr,                // 기존 포트 없음 / No existing port
    0,                      // 완료 키 (무시됨) / Completion key (ignored)
    0                       // CPU 코어 수만큼 스레드 / As many threads as CPU cores
);

// ✓ 변수 선언
int sessionCount = 0;      // 활성 세션 수 / Active session count
DWORD waitTime = 5000;     // 대기 시간(밀리초) / Wait time in milliseconds
```

### 3.2 주석 위치 (Comment Placement)

```cpp
// ✓ 라인 끝 주석 (줄 끝)
mSocket = socket(AF_INET, SOCK_STREAM, 0);  // 소켓 생성 / Create socket

// ✓ 위쪽 주석 (복잡한 경우)
// 소켓 옵션 설정: TCP_NODELAY를 활성화하여 Nagle 알고리즘 비활성화
// Set socket option: Enable TCP_NODELAY to disable Nagle's algorithm
setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, ...);

// ✓ 섹션 구분
//////////////////////////////////////////////////////////////////////////////
// 송신 처리 / Send handling
//////////////////////////////////////////////////////////////////////////////

// ✓ TODO/FIXME 마크
// TODO: RIO API 지원 추가 (Windows 8.1+) / Add RIO API support (Windows 8.1+)
// FIXME: 메모리 누수 확인 필요 / Need to check for memory leak
```

### 3.3 영어/한글 혼용 (Mixed Language)

```cpp
// ✓ 한글 먼저, 영어 뒤 (또는 영어 먼저, 한글 뒤)
int mSessionCount = 0;  // 세션 수 / Session count

// ✓ 긴 설명의 경우
// 이 함수는 IOCP 완료 이벤트를 처리하고 대응하는 세션의 콜백을 호출한다.
// This function handles IOCP completion events and calls the appropriate session callback.

// ✗ 피하기: 불명확한 약자
int cnt;  // ✗ cnt 대신 sessionCount를 사용하라

// ✗ 피하기: 주석 없음
int mValue;  // ✗ 값이 무엇인지 명확하지 않음
```

---

## 4. 열거형 (Enum)

### 4.1 열거형 주석 (Enum Comments)

```cpp
// IOCP 작업 타입을 정의하는 열거형
// Enumeration defining IOCP operation types
enum class IO_TYPE : unsigned char
{
    SEND = 0,               // 데이터 송신 / Send data
    RECV = 1,               // 데이터 수신 / Receive data
    ACCEPT = 2,             // 클라이언트 연결 수락 / Accept client connection
    CONNECT = 3,            // 서버 연결 / Connect to server
    DISCONNECT = 4,         // 연결 종료 / Disconnect
    FILE_WRITE = 5,         // 파일 비동기 쓰기 / Asynchronous file write
    JOB = 6,                // 작업 큐 처리 / Job queue processing
    MAX = 7                 // 최대값 (유효하지 않음) / Maximum value (invalid)
};

// ✓ enum class 사용 (타입 안전)
IO_TYPE ioType = IO_TYPE::SEND;

// ✗ 피하기: 평면 enum
enum IO_TYPE { SEND, RECV };  // 타입 안전성 부족
```

### 4.2 서비스 타입 예시

```cpp
// 네트워크 서비스 타입을 정의하는 열거형
// Enumeration defining network service types
enum class ServiceType : uint16
{
    SERVER = 0,                 // 서버 모드: 클라이언트 연결 수락 / Server mode: accept client connections
    CLIENT = 1,                 // 클라이언트 모드: 수동 연결 / Client mode: manual connection
    CLIENT_MANUAL = 2,          // 클라이언트 모드: 사용자 제어 / Client mode: user-controlled
    CLIENT_AUTO_RECONNECT = 3,  // 클라이언트 모드: 자동 재연결 / Client mode: auto-reconnect
    OFFLINE_MODE = 4,           // 오프라인 모드 / Offline mode
    MAX = 5                      // 최대값 (유효하지 않음) / Maximum value (invalid)
};
```

---

## 5. 네이밍 컨벤션 (Naming Convention)

### 5.1 클래스 및 구조체

```cpp
// ✓ PascalCase (대문자 시작)
class IocpCore { };
struct OverlappedEx { };
class SessionPool { };

// ✗ 피하기: camelCase 또는 snake_case
class iocpCore { };        // ✗
class iocp_core { };       // ✗
```

### 5.2 멤버 변수

```cpp
class MyClass
{
private:
    // ✓ 'mVariableName' 규칙 (m = member)
    int mSessionCount = 0;
    std::atomic<bool> mIsConnected = false;
    SOCKET mSocket = INVALID_SOCKET;
    std::vector<Session*> mSessionList;
    
public:
    // ✓ public 멤버는 소문자 시작
    int publicValue = 0;
};

// ✗ 피하기: 언더스코어 접두사
class MyClass
{
private:
    int _sessionCount;      // ✗ C++ 표준에 위험
    int Session_count;      // ✗
};
```

### 5.3 함수 및 메서드

```cpp
class Session
{
public:
    // ✓ PascalCase (대문자 시작)
    bool Connect();
    void Send(const uchar* data, int length);
    void Disconnect();
    bool IsConnected() const;
    
    // ✓ Get/Set 접두사
    int GetSessionId() const { return mSessionId; }
    void SetSessionId(int id) { mSessionId = id; }
    
private:
    // ✓ private 함수도 PascalCase
    void HandleRecvError(int errorCode);
    void CleanupResources();
};

// ✗ 피하기
bool connect();              // ✗ 소문자 시작
void sendData();             // ✗
int getSessionID();          // ✗ 'ID' 대신 'Id' 사용
```

### 5.4 상수 및 매크로

```cpp
// ✓ 상수: PascalCase 또는 UPPER_CASE
constexpr int MAX_SESSION_COUNT = 1000;
constexpr int DefaultBufferSize = 4096;
static constexpr std::chrono::milliseconds MAX_TIMEOUT = 5000ms;

// ✓ 매크로: UPPER_CASE (필요할 때만)
#define IOCP_BUFFER_SIZE 4096
#define IS_PLATFORM_WINDOWS 1

// ✗ 피하기: 혼합
#define maxSessions 100     // ✗ 혼합 스타일
const int max_sessions = 100;  // ✗ snake_case
```

### 5.5 로컬 변수

```cpp
// ✓ camelCase (소문자 시작)
int socketFd = 0;
bool isConnected = false;
std::string connectionName = "Server1";

// ✗ 피하기
int SocketFd = 0;           // ✗ PascalCase는 로컬 변수용 아님
int socket_fd = 0;          // ✗ snake_case는 피하기
```

---

## 6. 플랫폼 별 코드 분기 (Platform-Specific Code)

### 6.1 조건부 컴파일 (Conditional Compilation)

```cpp
// ✓ 플랫폼 정의 매크로 (필수 주석)
// Windows IOCP를 사용한 비동기 I/O 처리
// Windows uses IOCP for asynchronous I/O
#if defined(_WIN32) || defined(_WIN64)

    #include <winsock2.h>
    #include <mswsock.h>
    
    // Windows 전용: IOCP 커널 객체
    // Windows-specific: IOCP kernel object
    class IocpCore
    {
    private:
        HANDLE mCompletionPort;  // IOCP 핸들
    };

// ✓ Linux 대체 구현 (주석 필수)
// Linux epoll을 사용한 비동기 I/O 처리
// Linux uses epoll for asynchronous I/O
#elif defined(__linux__)

    #include <sys/epoll.h>
    #include <netinet/in.h>
    
    // Linux 전용: epoll 파일 디스크립터
    // Linux-specific: epoll file descriptor
    class IocpCore
    {
    private:
        int mEpollFd;  // epoll 파일 디스크립터
    };

// ✓ macOS 대체 구현
// macOS kqueue를 사용한 비동기 I/O 처리
// macOS uses kqueue for asynchronous I/O
#elif defined(__APPLE__)

    #include <sys/event.h>
    #include <netinet/in.h>
    
    class IocpCore
    {
    private:
        int mKqueueFd;  // kqueue 파일 디스크립터
    };

#else
    #error "Unsupported platform"
#endif
```

### 6.2 크로스 플랫폼 추상화

```cpp
// ✓ 플랫폼 독립적 인터페이스 정의
// Platform-independent interface definition
class AsyncIOProvider
{
public:
    virtual bool RegisterHandle(void* handle) = 0;
    virtual bool WaitForCompletion(int timeoutMs) = 0;
    virtual void PostCompletion(Job* job) = 0;
    virtual ~AsyncIOProvider() = default;
};

// ✓ 각 플랫폼별 구현
// Platform-specific implementations
#if defined(_WIN32) || defined(_WIN64)
class IocpAsyncIO : public AsyncIOProvider
{
    // Windows IOCP 구현
};
#elif defined(__linux__)
class EpollAsyncIO : public AsyncIOProvider
{
    // Linux epoll 구현
};
#endif
```

### 6.3 플랫폼별 타입 정의

```cpp
// ✓ 플랫폼별 타입 래핑
// Wrapping platform-specific types
#if defined(_WIN32) || defined(_WIN64)
    using SocketHandle = SOCKET;
    using IOHandle = HANDLE;
    using ErrorCode = DWORD;
#else
    using SocketHandle = int;
    using IOHandle = int;
    using ErrorCode = int;
#endif

// ✓ 사용 예시
SocketHandle socket = CreateSocket();  // 모든 플랫폼에서 작동 / Works on all platforms
```

---

## 7. C++ 표준 활용 (C++ Standard Usage)

### 7.1 C++17/20 기능

```cpp
// ✓ std::atomic 사용 (스레드 안전)
std::atomic<int> mSessionCount = 0;
std::atomic<bool> mIsConnected = false;

// ✓ std::optional (C++17)
#if __cplusplus >= 201703L
std::optional<int> GetSessionId() const;
#else
bool GetSessionId(int& outId) const;
#endif

// ✓ constexpr 활용
constexpr int MAX_BUFFER_SIZE = 65536;
static_assert(MAX_BUFFER_SIZE > 0, "Buffer size must be positive");

// ✓ 범위 기반 for 루프
for (Session* session : mSessionList)
{
    session->Update();
}

// ✗ 피하기: C++98 스타일
for (int i = 0; i < mSessionList.size(); ++i)
{
    mSessionList[i]->Update();
}
```

### 7.2 스마트 포인터 (Smart Pointers)

```cpp
// ✓ unique_ptr 사용 (유일한 소유권)
class SessionPool
{
private:
    std::unique_ptr<IocpObjectSession[]> mSessions;  // 배열 유일 소유
};

// ✓ shared_ptr 사용 (공유 소유권, 필요할 때만)
class ServiceCoordinator
{
private:
    std::shared_ptr<SessionPool> mSessionPool;
};

// ✗ 피하기: 수동 포인터 (메모리 누수 위험)
IocpObjectSession* session = new IocpObjectSession();  // ✗
```

### 7.3 C++ 표준 라이브러리

```cpp
// ✓ std::vector 사용
std::vector<Session*> mActiveSessions;

// ✓ std::unordered_set 사용
std::unordered_set<int> mConnectedSessionIds;

// ✓ std::string 사용 (wchar 대신)
std::wstring connectionName = L"ServerName";

// ✗ 피하기: 수동 배열 관리
Session** mSessions = new Session*[100];  // ✗ 메모리 관리 위험
```

---

## 8. 타입 안전성 (Type Safety)

### 8.1 정수 타입 표준화

```cpp
// ✓ C++11 표준 타입 사용
#include <cstdint>

std::uint8_t  byte = 0xFF;      // unsigned char 대신
std::uint16_t port = 8080;      // unsigned short 대신
std::uint32_t ip = 0x7F000001;  // unsigned int 대신
std::int32_t  sessionId = 1;    // int 대신
std::size_t   bufferSize = 1024; // size_t 사용

// ✗ 피하기: 플랫폼 의존적 타입
unsigned char buffer[1024];     // ✗ 크기 불명확
unsigned int count = 0;         // ✗ 플랫폼에 따라 크기 다름
```

### 8.2 bool 대신 상세한 이름

```cpp
// ✓ 명확한 이름 사용
bool mIsConnected = false;      // mConnected 보다 낫다
bool mHasError = false;         // mError 보다 명확
bool mIsInitialized = false;    // mInit 보다 나음

// ✗ 피하기: 축약형
bool mConn = false;             // ✗ 불명확
bool mErr = false;              // ✗
```

---

## 9. 메모리 관리 (Memory Management)

### 9.1 할당 해제 패턴

```cpp
// ✓ RAII (Resource Acquisition Is Initialization)
class Session
{
public:
    Session()
    {
        mBuffer = std::make_unique<uchar[]>(BUFFER_SIZE);  // 자동 할당
    }
    
    ~Session()
    {
        // mBuffer는 자동으로 해제됨 / mBuffer is automatically freed
    }
    
private:
    std::unique_ptr<uchar[]> mBuffer;
};

// ✗ 피하기: 수동 메모리 관리
class BadSession
{
public:
    BadSession() { mBuffer = new uchar[BUFFER_SIZE]; }
    ~BadSession() { delete[] mBuffer; }  // 잘못 호출 시 누수
    
private:
    uchar* mBuffer;
};
```

### 9.2 객체 풀 (Object Pool)

```cpp
// ✓ 객체 풀을 통한 메모리 재사용
class SessionPool
{
private:
    std::vector<std::unique_ptr<IocpObjectSession>> mPooledSessions;
    std::queue<IocpObjectSession*> mAvailableSessions;
    
public:
    IocpObjectSession* AcquireSession()
    {
        if (!mAvailableSessions.empty())
        {
            IocpObjectSession* session = mAvailableSessions.front();
            mAvailableSessions.pop();
            return session;  // 재사용
        }
        // 새 세션 생성
        auto session = std::make_unique<IocpObjectSession>();
        IocpObjectSession* ptr = session.get();
        mPooledSessions.push_back(std::move(session));
        return ptr;
    }
};
```

---

## 10. 함수 설계 (Function Design)

### 10.1 함수 선언 및 정의

```cpp
// ✓ 명확한 반환 타입과 파라미터
bool SendData(const uchar* data, int length);
int OnRecv(const uchar* buffer, int bufferLength);
void Disconnect(SessionKickReason reason);

// ✓ const 정확성 (const-correctness)
const InetAddress& GetPeerAddress() const;
bool IsConnected() const { return mIsConnected; }

// ✗ 피하기: 불명확한 파라미터
void Process(int a, int b, bool c);  // ✗ 각 파라미터의 의미가 불명확

// ✓ 좋은 예
void Process(int sessionId, int bufferSize, bool enableEncryption);
```

### 10.2 예외 처리

```cpp
// ✓ 명시적 오류 처리
bool Connect()
{
    if (mSocket == INVALID_SOCKET)
    {
        LOG_ERROR("Socket not created");
        return false;
    }
    
    try
    {
        // 연결 시도
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR("Connection failed: {}", ex.what());
        return false;
    }
    
    return true;
}

// ✗ 피하기: 예외 무시
try
{
    Connect();
}
catch (...)
{
    // 아무것도 하지 않음 / Do nothing
}
```

---

## 11. 로깅 규칙 (Logging Rules)

### 11.1 로그 레벨

```cpp
// ✓ 적절한 로그 레벨 사용
LOG(LL_DEBUG, LC_DEFAULT, L"Session {} connected", sessionId);      // 디버그 정보
LOG(LL_INFO, LC_DEFAULT, L"Server started on port {}", port);       // 일반 정보
LOG(LL_WARN, LC_DEFAULT, L"Recv buffer approaching limit");         // 경고
LOG(LL_ERROR, LC_DEFAULT, L"Send failed: error code {}", errorCode); // 오류
LOG(LL_FATAL, LC_DEFAULT, L"Critical failure in IOCP");             // 치명적 오류

// ✗ 피하기: 모든 곳에 로그
for (int i = 0; i < 1000; ++i)
{
    LOG(LL_DEBUG, LC_DEFAULT, L"Processing item {}", i);  // ✗ 너무 많음
}
```

### 11.2 로그 메시지 형식

```cpp
// ✓ 명확한 메시지
LOG(LL_INFO, LC_DEFAULT, L"Session {} connected from {}:{}", 
    sessionId, ipAddress, port);

// ✗ 피하기: 불명확한 메시지
LOG(LL_INFO, LC_DEFAULT, L"Session connected");  // ✗ 어느 세션?
LOG(LL_ERROR, LC_DEFAULT, L"Error");             // ✗ 어떤 에러?
```

---

## 12. 정리 (Summary)

### 필수 규칙 (MUST)

- [ ] **Allman 스타일 괄호** - 모든 컨트롤 구조
- [ ] **영문과 한글 주석** - 모든 코드 라인
- [ ] **enum class 사용** - 타입 안전성
- [ ] **PascalCase 클래스** - MyClass
- [ ] **mVariableName 멤버** - m = member
- [ ] **camelCase 로컬 변수** - localVariable
- [ ] **플랫폼 분기 주석** - #ifdef 블록 설명
- [ ] **std::unique_ptr 사용** - 메모리 안전성
- [ ] **const 정확성** - 참조 제한

### 강력 권장 (SHOULD)

- [ ] C++17/20 기능 활용
- [ ] 범위 기반 for 루프
- [ ] std::atomic 동기화
- [ ] 명확한 로그 메시지
- [ ] 단위 테스트 작성

### 피해야 할 것 (MUST NOT)

- [ ] ✗ 1TBS 스타일
- [ ] ✗ 탭 들여쓰기
- [ ] ✗ 라인 끝 공백
- [ ] ✗ 주석 없는 코드
- [ ] ✗ 신규 코드의 C 스타일 배열
- [ ] ✗ 예외 무시 (empty catch)

---

## 참고 자료 (References)

- C++ Core Guidelines: https://github.com/isocpp/CppCoreGuidelines
- Google C++ Style Guide: https://google.github.io/styleguide/cppguide.html
- RAON Server Engine 기존 코드
- Windows IOCP 공식 문서

