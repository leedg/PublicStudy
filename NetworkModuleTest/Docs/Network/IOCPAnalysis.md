# IOCP 아키텍처 분석

**작성일**: 2026-01-27  
**분석 대상**: E:\Work\RAON\Server\ServerEngine  
**목표**: IOCP 구현 패턴 분석 및 새로운 네트워크 라이브러리 설계 참고

---

## 1. IOCP 아키텍처 개요

### 1.1 핵심 컴포넌트 (Core Components)

```
┌─────────────────────────────────────────────────────────────┐
│                      IocpCore                                │
│  - Windows IOCP 핸들 관리                                     │
│  - ServiceCoordinator 리스트 관리                             │
│  - GetQueuedCompletionStatus (GQCS) 호출                     │
│  - PostQueuedCompletionStatus (PQCS) for Job                │
└─────────────────────────────────────────────────────────────┘
         │                      │                    │
         ├──────────────────────┴──────────────────┬─────────┐
         ↓                                         ↓         ↓
  ┌─────────────────┐              ┌──────────────────────┐ │
  │ServiceCoordinator│              │IocpObjectListener    │ │
  │- 서비스 타입관리 │              │- Accept 요청         │ │
  │- SessionPool관리 │              │- 세션 생성           │ │
  │- 연결/종료 처리  │              │- 세션 카운트 관리    │ │
  └─────────────────┘              └──────────────────────┘ │
         │                                                    │
         ├─────────────────────────┬────────────────────────┘
         ↓                         ↓
  ┌─────────────────────┐   ┌──────────────────────┐
  │  SessionPool        │   │ IocpObjectSession    │
  │- 세션 풀 관리       │   │- 송수신 처리         │
  │- Circular Queue     │   │- 버퍼 관리           │
  │- AliveSessionSet    │   │- RefCount 관리       │
  └─────────────────────┘   └──────────────────────┘
```

### 1.2 ServiceCoordinator 역할

RAON ServerEngine에서 `ServiceCoordinator`는 네트워크 서비스의 생명주기를 관리하는 핵심 클래스입니다.

| 역할 | 설명 |
|------|------|
| **서비스 초기화** | IocpCore와 연결하여 IOCP 등록 |
| **세션 풀 관리** | SessionPool을 통해 세션 생성/재사용 |
| **리스너 관리** | 서버 모드에서 IocpObjectListener 생성 |
| **연결 관리** | CLIENT 타입의 연결 요청 처리 |
| **자동 재연결** | CLIENT_AUTO_RECONNECT 타입 지원 |

**ServiceType 열거형**:
```cpp
enum class ServiceType : uint16
{
    SERVER = 0,                 // 서버 모드: 클라이언트 연결 수락
    CLIENT,                     // 클라이언트 모드: 수동 연결
    CLIENT_MANUAL,              // 클라이언트 모드: 수동 제어
    CLIENT_AUTO_RECONNECT,      // 클라이언트 모드: 자동 재연결
    OFFILE_MODE,                // 오프라인 모드
};
```

---

## 2. IOCP 핵심 개념 (Core Concepts)

### 2.1 OverlappedEx 구조체

```cpp
struct OverlappedEx : public OVERLAPPED, public Disposable
{
    IO_TYPE mIoType;    // 작업 타입 (SEND, RECV, CONNECT, DISCONNECT 등)
    WSABUF  mWsaBuf;    // Winsock 버퍼 구조체
};
```

**IO_TYPE 열거형**:
| 값 | 설명 | 사용처 |
|---|------|--------|
| SEND | 데이터 송신 | WSASend() |
| PRE_RECV | 수신 전 준비 | 버퍼 할당 |
| RECV | 데이터 수신 | WSARecv() |
| ACCEPT | 클라이언트 연결 수락 | AcceptEx() |
| CONNECT | 서버에 연결 | ConnectEx() |
| DISCONNECT | 연결 종료 | DisconnectEx() |
| FILE_WRITE | 파일 비동기 쓰기 | 로그 저장 |
| JOB | 작업 큐 | PostQueuedCompletionStatus() |

### 2.2 IocpObject 인터페이스

```cpp
class IocpObject : public Disposable
{
public:
    virtual SessionType GetSessionType();
    virtual HANDLE GetHandle() = 0;
    virtual void HandleIocp(OverlappedEx* overlapped, int bytes) = 0;
};
```

- **GetHandle()**: IOCP에 등록할 socket 또는 file handle 반환
- **HandleIocp()**: IOCP 완료 시 호출되는 콜백 함수

---

## 3. IocpCore 동작 원리

### 3.1 초기화 (StartUp)

```cpp
IocpCore::IocpCore()
{
    StartUp();
    // CreateIoCompletionPort: IOCP 커널 객체 생성
    mCompletionPort = CreateIoCompletionPort(
        INVALID_HANDLE_VALUE,   // 새 IOCP 생성
        nullptr,
        0,
        0  // 동시 처리 스레드 수 (0 = CPU 코어 수)
    );
}
```

**StartUp() 수행 작업**:
1. WSAStartup(): Winsock2 초기화
2. ConnectEx() 호출: ExtensionFunction 로드
3. DisconnectEx() 호출: ExtensionFunction 로드
4. GetHostName(): 로컬 IP 주소 획득

### 3.2 서비스 등록 (RegisterXXXService)

```cpp
// 서버 모드
ServiceCoordinator* RegisterServerService(
    uint16 port, 
    int serviceCount, 
    SessionPool* sessionPool
);

// 클라이언트 모드
ServiceCoordinator* RegisterClientService(
    InetAddress&& addr, 
    int serviceCount, 
    SessionPool* sessionPool, 
    bool manual = false
);

// 자동 재연결 클라이언트
ServiceCoordinator* RegisterAutoReconnectClientService(
    InetAddress&& addr, 
    int serviceCount, 
    SessionPool* sessionPool
);
```

각 서비스는:
1. ServiceCoordinator 객체 생성
2. Initialize() 호출로 IOCP에 등록
3. mServiceCoordinatorList에 추가

### 3.3 IOCP 객체 등록 (RegisterIocpObject)

```cpp
bool IocpCore::RegisterIocpObject(IocpObject* iocpObject)
{
    // 객체의 handle을 IOCP에 연결
    return nullptr != CreateIoCompletionPort(
        iocpObject->GetHandle(),    // Socket 또는 File Handle
        mCompletionPort,            // 기존 IOCP 포트
        reinterpret_cast<ULONG_PTR>(iocpObject),  // Completion Key
        0
    );
}
```

**Completion Key**: IocpObject 포인터를 저장하여 GQCS 반환 시 어느 객체의 작업인지 식별

### 3.4 메시지 루프 (HandleIocp)

```cpp
void IocpCore::HandleIocp(int timeOut)
{
    DWORD numbOfBytes = 0;
    IocpObject* iocpObject = nullptr;
    OVERLAPPED* over = nullptr;
    
    // GQCS: 완료된 IO 대기
    if (GetQueuedCompletionStatus(
        mCompletionPort, 
        &numbOfBytes, 
        (PULONG_PTR)&iocpObject, 
        (LPOVERLAPPED*)&over, 
        timeOut
    ))
    {
        OverlappedEx* overlapped = static_cast<OverlappedEx*>(over);
        HandleNormalIocp(iocpObject, overlapped, numbOfBytes);
    }
    // 에러 처리
    else
    {
        int errorCode = WSAGetLastError();
        // WAIT_TIMEOUT, ERROR_SEM_TIMEOUT 등 분류 처리
    }
}
```

**에러 분류**:
- **WAIT_TIMEOUT (258)**: 정상 타임아웃
- **ERROR_SEM_TIMEOUT, ERROR_NETNAME_DELETED** 등: 복구 가능
- **ERROR_ABANDONED_WAIT_0 (735)**: IOCP 포트 종료

### 3.5 작업 큐 (PushIocpJob)

```cpp
void IocpCore::PushIocpJob(Job* job)
{
    OverlappedEx* overlapped = xnew<OverlappedEx>(job);
    PostQueuedCompletionStatus(
        mCompletionPort,
        0,
        (ULONG_PTR)IocpObjectJob::GetIocpObjectJob(),
        overlapped
    );
}
```

- IO 없이 IOCP 큐에 작업 추가 (비동기 콜백 처럼 사용)
- MainThread의 HandleIocp()에서 처리됨

---

## 4. IocpObjectSession 상세

### 4.1 세션 생명주기

```
[생성]
  ↓
[연결 요청] → OnConnect() 콜백
  ↓
[수신 대기] → OnRecv() 콜백 반복
  ↓
[송신] → OnSend() 콜백
  ↓
[연결 종료] → OnDisconnect() 콜백
  ↓
[정리]
```

### 4.2 주요 상태 변수

| 변수 | 타입 | 설명 |
|------|------|------|
| mRefCount | atomic<int> | 참조 카운트 (-1 = 미사용) |
| mIsConnected | atomic<int> | 연결 상태 |
| mInResetSession | atomic<int> | 세션 리셋 중 |
| mSendPendingSize | atomic<int> | 대기 중인 송신 데이터 크기 |
| mReserveSendCount | atomic<int> | 예약된 송신 패킷 수 |

### 4.3 송수신 버퍼 관리

**송신 (Send)**:
```
User SendBuffer 
    ↓
SendBufferQueue (mSendPendingQueue)
    ↓
ReserveBuffer 또는 즉시 송신
    ↓
WSASEND (overlapped)
    ↓
OnSend() 콜백
```

**수신 (Recv)**:
```
mRecvBuf (고정 크기 버퍼)
    ↓
WSARecv (overlapped)
    ↓
OnRecv() 콜백
    ↓
mRecvBufStart/mRecvBufEnd 조정 (데이터 연속성 유지)
```

### 4.4 RefCount 메커니즘

```cpp
int AddRef()        // 참조 증가 (세션 사용 시)
int ReleaseRef()    // 참조 감소 (세션 반환 시)
```

**목적**: 멀티스레드 환경에서 세션의 안전한 해제
- IOCP 완료 핸들러가 실행 중에도 다른 스레드에서 세션 삭제 방지

---

## 5. SessionPool 구조

### 5.1 순환 큐 (Circular Queue)

```cpp
IocpObjectSession** mSessionPool;   // 배열 기반
int mPopIndex;  // Acquire 인덱스
int mPushIndex; // Release 인덱스
```

**Acquire**: mPopIndex에서 취득하여 mPopIndex 증가
**Release**: mPushIndex에 반환하여 mPushIndex 증가

### 5.2 AliveSessionSet

```cpp
AliveSessionSet mAliveSessionSet;  // 활성 세션 추적
```

- Acquire한 세션들을 추적
- SessionPool 리셋 시 활성 세션들을 정리

---

## 6. IocpObjectListener (서버)

### 6.1 Accept 처리

```cpp
class IocpObjectListener final : public IocpObject
{
private:
    SOCKET mSocket;
    std::atomic<int> mSessionCount;  // 연결된 세션 수
    ServiceCoordinator* mServiceCoordinator;
    
public:
    bool Listen(const InetAddress& address);
    void RequestAccept();
    void ProcessAccept(OverlappedEx* overlapped);
};
```

**흐름**:
1. Listen() → Socket 생성 및 바인드
2. RequestAccept() → AcceptEx() 호출
3. IOCP 완료 → ProcessAccept() → SessionPool에서 세션 획득
4. 새 세션 초기화 및 Recv 요청

### 6.2 세션 카운트

```cpp
int IncreaseSessionCount();  // 연결 시
int DecreaseSessionCount();  // 종료 시
```

동시 연결 수 모니터링

---

## 7. 에러 처리 및 엣지 케이스

### 7.1 IOCP 에러 처리 (IocpCore::HandleIocp)

```cpp
switch (errorCode)
{
case WAIT_TIMEOUT:              // 정상 (타임아웃)
case ERROR_SEM_TIMEOUT:         // 복구 가능
case ERROR_NETNAME_DELETED:     // 피어 강제 종료
case ERROR_CONNECTION_REFUSED:  // 연결 거부
case ERROR_INVALID_NETNAME:     // 잘못된 네트워크 이름
case WSA_OPERATION_ABORTED:     // 작업 중단 (595)
case ERROR_CONNECTION_ABORTED:  // 연결 중단
case ERROR_NETWORK_UNREACHABLE: // 네트워크 도달 불가
    HandleNormalIocp(iocpObject, overlapped, numbOfBytes);
    break;

case ERROR_ABANDONED_WAIT_0:    // GQCS 종료 신호
    // IOCP 포트 종료 (CloseHandle 호출됨)
    break;

default:                         // 예상치 못한 에러
    // 로깅 및 특수 처리
}
```

### 7.2 세션 에러 처리

```cpp
enum class IO_SESSION_KICK_REASON : uint8
{
    IO_SESSION_KICK_REASON_NO_LOG,
    IO_SESSION_KICK_REASON_UNKNOW,
    IO_SESSION_KICK_REASON_PACKET_ERROR,
    IO_SESSION_KICK_REASON_DISCONNECT,
    IO_SESSION_KICK_REASON_SEND_ERROR,
    IO_SESSION_KICK_REASON_RECV_0,              // Graceful close
    IO_SESSION_KICK_REASON_RECV_BUFFER_OVER_FULL,
    IO_SESSION_KICK_REASON_HANDLE_IOCP_ERROR,
    // ... 등
};
```

---

## 8. 멀티스레드 안전성 (Thread-Safety)

### 8.1 원자적 연산 사용

```cpp
std::atomic<int> mRefCount;        // RefCount 관리
std::atomic<int> mIsConnected;     // 연결 상태
std::atomic_flag mInitialized;     // 초기화 완료
std::atomic<int> mSessionCount;    // 세션 카운트
```

### 8.2 Lock 사용

```cpp
DECLARE_SINGLE_LOCK;  // 매크로를 통한 뮤텍스 선언
```

- SessionPool: 세션 획득/반환 시
- ReserveSendCount: 송신 예약 큐 관리

---

## 9. 성능 고려사항 (Performance)

### 9.1 SendBuffer 최적화

- **ReserveBuffer**: 개별 패킷을 미리 모아서 한 번에 송신
- **ConcatSendBuffer**: 여러 SendBuffer를 한 WSASend() 호출로 처리
- **mSendPendingQueue**: ThreadSafeQueue로 lock-free 아키텍처 가능

### 9.2 RecvBuffer 관리

```cpp
int mRecvBufStart;      // 데이터 시작 위치
int mRecvBufEnd;        // 데이터 종료 위치
```

데이터 연속성 유지로 불필요한 복사 최소화

### 9.3 메모리 관리

- **MemoryPool**: 버퍼, 구조체 재사용
- **RefCount**: 세션 중복 해제 방지
- **AliveSessionSet**: 명시적 세션 추적

---

## 10. 주요 파일 구조

```
ServerEngine/Network/
├── Iocp/
│   ├── IocpCore.h / IocpCore.cpp          # IOCP 핵심 관리
│   ├── IocpObject.h                       # 추상 인터페이스
│   ├── IocpObjectListener.h / cpp         # 서버 리스너
│   ├── IocpObjectJob.h / cpp              # Job 처리
│   └── IocpObjectFile.h / cpp             # 파일 비동기 I/O
├── Session/
│   ├── IocpObjectSession.h / cpp          # 세션 추상 클래스
│   ├── IocpObjectSession_Recv.cpp         # 수신 구현
│   ├── IocpObjectSession_Send.cpp         # 송신 구현
│   ├── IocpObjectSession_SockOpt.cpp      # 소켓 옵션
│   ├── SessionPool.h / cpp                # 세션 풀
│   └── CryptoSession.h / cpp              # 암호화 세션
├── ServiceCoordinator.h / cpp             # 서비스 조정자
├── NetworkTypeDef.h / cpp                 # 타입 정의
└── Buffer/
    ├── SendBufferChunkPool.h / cpp
    └── SendBufferHelper.h / cpp
```

---

## 11. 중요한 설계 패턴

### 11.1 Strategy 패턴
- `ServiceType`에 따라 다른 동작 구현
- SERVER/CLIENT 모드 분리

### 11.2 Object Pool 패턴
- SessionPool을 통한 세션 재사용
- MemoryPool을 통한 버퍼 재사용

### 11.3 Template Method 패턴
- IocpObjectSession의 추상 메서드
- 하위 클래스에서 OnConnect, OnSend, OnRecv, OnDisconnect 구현

### 11.4 Observer 패턴
- IOCP 완료 → HandleIocp() 콜백
- Job 처리 → IocpObjectJob

---

## 12. 향후 개선 고려사항

### Windows 전용 제약
- ConnectEx / DisconnectEx: Windows Vista 이상 필수
- IOCP: Windows 전용
- RIO (Registered I/O): Windows 8.1 이상, 더 높은 성능

### 크로스 플랫폼 추상화 필요
- epoll (Linux): IOCP와 유사한 API
- kqueue (macOS/BSD): 비동기 이벤트
- poll/select: 하위 호환성

### 메모리 안전성
- 현재: 수동 메모리 관리 (xnew/xdelete)
- 개선: Smart Pointer 더 적극적 사용
- C++17/20: std::unique_ptr, std::shared_ptr

---

## 참고 (References)

- Windows IOCP 공식 문서
- RAON Server 실제 구현
- 멀티스레드 동기화 패턴
- 네트워크 프로토콜 성능 최적화

