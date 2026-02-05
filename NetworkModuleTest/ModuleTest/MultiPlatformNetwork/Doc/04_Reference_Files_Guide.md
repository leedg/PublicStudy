# 참고 파일 가이드

**목적**: IOCP 기반 네트워크 라이브러리 구축 시 참고해야 할 핵심 파일 목록  
**생성일**: 2026-01-27  
**분류**: 파일별 역할, 크기, 핵심 개념

---

## 🎯 파일 우선순위 및 학습 순서

### Tier 1 (필수 - 먼저 읽어야 함)

1. **NetworkTypeDef.h** (2.6 KB)
   - **역할**: 기본 타입 및 구조 정의
   - **핵심 내용**:
     - `InetAddress` / `InetAddress6` 클래스 (IPv4/IPv6)
     - `IO_TYPE` enum (SEND, RECV, ACCEPT, CONNECT, DISCONNECT, JOB)
     - `SessionType` enum (UNKNOWN, DIRECTOR, AUTH, WORLD, GATEWAY)
     - `OverlappedEx` 구조체 (OVERLAPPED + mIoType + mWsaBuf)
   - **의존성**: ws2ipdef.h (Windows Winsock)
   - **학습 포인트**: 기본 네트워크 타입 정의

2. **Iocp/IocpObject.h** (1.5 KB)
   - **역할**: IOCP 객체의 추상 기본 클래스
   - **핵심 내용**:
     ```cpp
     class IocpObject : public Disposable
     {
     public:
         virtual SessionType GetSessionType();
         virtual HANDLE GetHandle() = 0;  // Socket 또는 Handle 반환
         virtual void HandleIocp(OverlappedEx* overlapped, int bytes) = 0;
     };
     ```
   - **의존성**: OverlappedEx, IO_TYPE
   - **학습 포인트**: 다형성을 통한 이벤트 처리 패턴

3. **Iocp/IocpCore.h** (2.4 KB)
   - **역할**: IOCP 커널 객체 관리 및 메시지 루프
   - **핵심 내용**:
     ```cpp
     class IocpCore
     {
     private:
         HANDLE mCompletionPort;              // IOCP 커널 객체
         ServiceCoordinatorList mServices;    // 등록된 서비스
         int mLastErrorCode;
     
     public:
         ServiceCoordinator* RegisterServerService(uint16 port, ...);
         ServiceCoordinator* RegisterClientService(InetAddress&&, ...);
         bool RegisterIocpObject(IocpObject* iocpObject);
         void HandleIocp(int timeOut);      // GQCS 루프
         void PushIocpJob(Job* job);        // PQCS
     };
     ```
   - **의존성**: ServiceCoordinator, IocpObject, OverlappedEx
   - **학습 포인트**: GQCS/PQCS 메커니즘, 에러 처리

---

### Tier 2 (핵심 구현 - 다음 읽어야 함)

4. **Session/IocpObjectSession.h** (9.0 KB)
   - **역할**: 네트워크 세션의 추상 기본 클래스
   - **주요 멤버**:
     ```cpp
     class IocpObjectSession : public IocpObject
     {
     protected:
         SOCKET mSocket;                      // 소켓
         InetAddress mPeerAddress;            // 피어 주소
         std::atomic<int> mRefCount = -1;     // 참조 카운트
         std::atomic<int> mIsConnected = 0;   // 연결 상태
         
         int mIoBufferSize;
         uchar* mSendBuf;                     // 송신 버퍼
         uchar* mRecvBuf;                     // 수신 버퍼
         
         int mRecvBufStart, mRecvBufEnd;      // 데이터 범위
         std::atomic<int> mSendPendingSize;   // 대기 송신 크기
         
         SendBufferQueue mSendPendingQueue;   // 송신 예약 큐
         SendBufferVector mReserveBufferVector; // 예약 버퍼
     };
     ```
   - **주요 메서드**:
     ```cpp
     bool Connect();
     void Send(SendBufferRef sendBuffer, int len);
     void Disconnect(IO_SESSION_KICK_REASON reason);
     bool Reset();
     
     // 콜백 함수 (하위 클래스에서 구현)
     virtual void OnConnect(bool success);
     virtual void OnSend(int len, uint16 packetId);
     virtual int OnRecv(const uchar* buf, int len);
     virtual void OnDisconnect();
     
     // IOCP 콜백
     void HandleIocp(OverlappedEx* overlapped, int bytes) override;
     void HandleIocpConnect(OverlappedEx* overlapped);
     void HandleIocpSend(OverlappedEx* overlapped, DWORD bytes);
     void HandleIocpPreRecv(OverlappedEx* overlapped);
     void HandleIocpRecv(OverlappedEx* overlapped, DWORD bytes);
     ```
   - **의존성**: IocpObject, SendBuffer, SessionPool
   - **학습 포인트**: 세션 라이프사이클, RefCount, 버퍼 관리

5. **Iocp/IocpObjectListener.h** (1.1 KB)
   - **역할**: 서버 리스너 (클라이언트 연결 수락)
   - **주요 내용**:
     ```cpp
     class IocpObjectListener : public IocpObject
     {
     private:
         SOCKET mSocket;
         std::atomic<int> mSessionCount = 0;
         ServiceCoordinator* mServiceCoordinator;
     
     public:
         bool Listen(const InetAddress& address);
         void RequestAccept();
         int IncreaseSessionCount();
         int DecreaseSessionCount();
     };
     ```
   - **의존성**: IocpObject, ServiceCoordinator
   - **학습 포인트**: AcceptEx 패턴

6. **ServiceCoordinator.h** (2.4 KB)
   - **역할**: 서비스 생명주기 관리 (IocpCore와 세션 풀을 연결)
   - **주요 내용**:
     ```cpp
     class ServiceCoordinator : public Disposable
     {
     private:
         ServiceType mServiceType;         // SERVER/CLIENT/AUTO_RECONNECT
         IocpCore* mIocpCore;
         SessionPool* mSessionPool;
         IocpObjectListener* mListener;    // 리스너 (SERVER 전용)
         InetAddress mInetAddress;
     
     public:
         bool Initialize(IocpCore* iocpCore);
         IocpObjectSession* AcquireSession();
         void ReleaseSession(IocpObjectSession* session);
         bool ManualConnect(IocpObjectSession** outSession);
     };
     ```
   - **의존성**: IocpCore, SessionPool, IocpObjectListener
   - **학습 포인트**: 서비스 타입별 동작 분리

7. **Session/SessionPool.h** (1.4 KB)
   - **역할**: 세션 객체 풀 (재사용)
   - **주요 내용**:
     ```cpp
     class SessionPool
     {
     protected:
         IocpObjectSession** mSessionPool;    // 순환 큐 배열
         int mPopIndex = 0;                   // 획득 인덱스
         int mPushIndex = 0;                  // 반환 인덱스
         AliveSessionSet mAliveSessionSet;    // 활성 세션 추적
     
     public:
         bool Initialize(ServiceCoordinator* serviceCoordinator, int poolCount);
         IocpObjectSession* AcquireSession();
         void ReleaseSession(IocpObjectSession* session);
         void ResetSessionPool();
     };
     ```
   - **의존성**: IocpObjectSession
   - **학습 포인트**: 순환 큐, 메모리 재사용

---

### Tier 3 (구현 상세 - 이후 읽어야 함)

8. **Session/IocpObjectSession.cpp** (15 KB)
   - **역할**: 세션 핵심 구현
   - **주요 함수**:
     - `Connect()`: 연결 시작 (ConnectEx 호출)
     - `Send(SendBufferRef, int)`: 송신 예약
     - `Disconnect()`: 연결 종료
     - `Reset()`: 세션 재초기화
     - `HandleIocp()`: 모든 IO 완료 처리의 진입점
     - `AddRef()` / `ReleaseRef()`: RefCount 관리

9. **Session/IocpObjectSession_Send.cpp** (18 KB)
   - **역할**: 송신 로직 상세 구현
   - **주요 함수**:
     ```cpp
     bool SendData(uchar* buffer, int len);
     bool SendOverlapped(OverlappedEx* overlapped);
     void ReserveSend(SendBufferRef, int len);
     void FlushSend();
     bool FillSendBufferFromReserveList(int& index, int& count);
     bool FillSendBuffer(SendBufferEntity*, int&, int&);
     void ConcatSendBuffer(SendBufferEntity*, int&);
     void OnSendComplete(int count);
     ```
   - **학습 포인트**: 
     - SendBuffer 병합 (여러 작은 버퍼 → 하나의 WSASend)
     - WSABUF 배열 사용
     - SendPendingQueue 관리

10. **Session/IocpObjectSession_Recv.cpp** (5.7 KB)
    - **역할**: 수신 로직 구현
    - **주요 함수**:
      ```cpp
      void RecvData();
      void HandleIocpPreRecv(OverlappedEx*);
      void HandleIocpRecv(OverlappedEx*, DWORD bytes);
      ```
    - **학습 포인트**: 
      - mRecvBufStart/End 관리
      - OnRecv 콜백
      - 데이터 연속성

11. **Session/IocpObjectSession_SockOpt.cpp** (4.8 KB)
    - **역할**: 소켓 옵션 설정
    - **주요 함수**:
      ```cpp
      void SetKernelSendBufSize(int size);
      void SetKernelRecvBufSize(int size);
      void TurnOffNagle();
      void SetKeepAliveOpt(ULONG, ULONG);
      void SetReuseAddr(bool);
      void SetLinger(int, int);
      ```

12. **Iocp/IocpCore.cpp** (8.3 KB)
    - **역할**: IOCP 핵심 구현
    - **주요 함수**:
      ```cpp
      IocpCore();  // CreateIoCompletionPort
      ~IocpCore(); // CloseHandle, WSACleanup
      void StartUp();  // Winsock 초기화, ConnectEx/DisconnectEx 로드
      
      // 서비스 등록
      ServiceCoordinator* RegisterServerService(...);
      ServiceCoordinator* RegisterClientService(...);
      
      // IOCP 관리
      bool RegisterIocpObject(IocpObject*);
      void PushIocpJob(Job*);
      void HandleIocp(int timeOut);  // GQCS 루프 + 에러 처리
      ```
    - **학습 포인트**:
      - CreateIoCompletionPort 초기화
      - GQCS에서 에러 코드 분류
      - WAIT_TIMEOUT, ERROR_NETNAME_DELETED 등 처리

---

### Tier 4 (선택적 - 필요시 읽어야 함)

13. **Buffer/SendBufferHelper.h** (50 줄)
    - **역할**: SendBuffer를 안전하게 사용하기 위한 RAII 래퍼
    - **주요 내용**:
      ```cpp
      class SendBufferHelper
      {
      private:
          SendBufferRef mSendBuffer;
          int mSize;
      public:
          SendBufferHelper(SendBufferRef, int size);
          ~SendBufferHelper();  // CloseChunk 호출
          
          operator SendBufferRef() const;
          SendBufferRef operator->();
      };
      ```

14. **Buffer/SendBufferChunkPool.h/cpp**
    - **역할**: SendBuffer 청크 메모리 풀
    - **개념**: 
      - SendBuffer는 여러 개의 Chunk로 구성
      - Chunk는 메모리 풀에서 재사용
      - OpenChunk → 데이터 작성 → CloseChunk

15. **Buffer/SendBufferHelper.cpp**
    - **구현**: SendBufferHelper의 실제 구현

16. **Iocp/IocpObjectListener.cpp** (9.2 KB)
    - **역할**: 리스너 구현
    - **주요 함수**:
      ```cpp
      bool Listen(const InetAddress&);
      void RequestAccept();      // AcceptEx 호출
      void ProcessAccept(...);   // 새 세션 초기화
      ```

17. **Iocp/IocpObjectJob.h/cpp**
    - **역할**: IOCP Job 처리
    - **개념**: PostQueuedCompletionStatus로 비동기 작업 큐잉

18. **Iocp/IocpObjectFile.h/cpp**
    - **역할**: 파일 비동기 I/O
    - **개념**: 로그 파일 같은 비동기 쓰기

19. **Session/CryptoSession.h/cpp** (920 bytes + 7KB)
    - **역할**: IocpObjectSession의 암호화 버전
    - **개념**: 데이터 암호화/복호화 추가

20. **ServiceCoordinator.cpp** (5.0 KB)
    - **역할**: ServiceCoordinator 구현

21. **Session/SessionPool.cpp** (4.1 KB)
    - **역할**: SessionPool 구현 (순환 큐 로직)

---

## 📊 파일 크기 및 의존성 맵

```
┌─────────────────────────────────────────────────────────┐
│ NetworkTypeDef.h/cpp (2.6 + 7.7 KB)                    │
│ ↓ 기본 타입 (InetAddress, IO_TYPE, OverlappedEx)       │
├─────────────────────────────────────────────────────────┤
│ Iocp/IocpObject.h (1.5 KB)                             │
│ ↓ IOCP 객체 추상 클래스                                 │
├──────────────────────────────────────────────────────────┤
│ Iocp/IocpCore.h/cpp (2.4 + 8.3 KB)                     │
│ ↓ IOCP 커널 관리, GQCS/PQCS 루프                        │
├──────────────────────────────────────────────────────────┤
│ ServiceCoordinator.h/cpp (2.4 + 5.0 KB)                │
│ ↓ 서비스 생명주기 관리                                   │
├──────────────────────────────────────────────────────────┤
│ Session/SessionPool.h/cpp (1.4 + 4.1 KB)               │
│ ↓ 세션 객체 풀                                           │
├──────────────────────────────────────────────────────────┤
│ Session/IocpObjectSession.h (9.0 KB)                   │
│ ├── IocpObjectSession.cpp (15 KB)                       │
│ ├── IocpObjectSession_Send.cpp (18 KB)                  │
│ ├── IocpObjectSession_Recv.cpp (5.7 KB)                 │
│ └── IocpObjectSession_SockOpt.cpp (4.8 KB)              │
│ ↓ 세션 실제 구현 (SendBuffer, RecvBuffer, IOCP 핸들)    │
├──────────────────────────────────────────────────────────┤
│ Iocp/IocpObjectListener.h/cpp (1.1 + 9.2 KB)           │
│ ↓ 리스너 (AcceptEx)                                      │
├──────────────────────────────────────────────────────────┤
│ Buffer/SendBufferHelper.h/cpp (50 lines + ...)          │
│ Buffer/SendBufferChunkPool.h/cpp                        │
│ ↓ SendBuffer 메모리 관리                                │
└──────────────────────────────────────────────────────────┘
```

---

## 🔑 핵심 코드 패턴 (Copy-Paste Reference)

### 1. IOCP 초기화

```cpp
// IocpCore::IocpCore()
mCompletionPort = CreateIoCompletionPort(
    INVALID_HANDLE_VALUE,   // 새 IOCP 생성
    nullptr,
    0,
    0  // 동시 처리 스레드 수 (0 = CPU 코어 수)
);
```

### 2. 객체를 IOCP에 등록

```cpp
// IocpCore::RegisterIocpObject()
CreateIoCompletionPort(
    iocpObject->GetHandle(),        // Socket handle
    mCompletionPort,
    reinterpret_cast<ULONG_PTR>(iocpObject),  // Completion Key
    0
);
```

### 3. GQCS 메시지 루프

```cpp
// IocpCore::HandleIocp()
DWORD numbOfBytes = 0;
IocpObject* iocpObject = nullptr;
OVERLAPPED* over = nullptr;

if (GetQueuedCompletionStatus(
    mCompletionPort, 
    &numbOfBytes, 
    (PULONG_PTR)&iocpObject,
    (LPOVERLAPPED*)&over, 
    timeOut
))
{
    OverlappedEx* overlapped = static_cast<OverlappedEx*>(over);
    iocpObject->HandleIocp(overlapped, numbOfBytes);
}
else
{
    int errorCode = WSAGetLastError();
    // 에러 처리
}
```

### 4. Job 비동기 큐잉

```cpp
// IocpCore::PushIocpJob()
OverlappedEx* overlapped = xnew<OverlappedEx>(job);
PostQueuedCompletionStatus(
    mCompletionPort,
    0,
    (ULONG_PTR)IocpObjectJob::GetIocpObjectJob(),
    overlapped
);
```

### 5. SendBuffer 병합 송신

```cpp
// IocpObjectSession_Send::SendData()
WSABUF wsaBufs[MAX_SEND_BUFFER_COUNT];
int count = 0;

for (auto* entity : mReserveBufferVector)
{
    wsaBufs[count].buf = entity->mSendBuffer->GetData();
    wsaBufs[count].len = entity->mSendBufferLen;
    count++;
}

WSASend(mSocket, wsaBufs, count, nullptr, 0, overlapped, nullptr);
```

### 6. 세션 RefCount 패턴

```cpp
// 세션 사용 시작
int refCount = session->AddRef();
if (refCount <= 0) {
    // 세션 이미 해제됨
    return false;
}

// 세션 사용...

// 세션 사용 끝
int newRefCount = session->ReleaseRef();
```

### 7. RecvBuffer 관리

```cpp
// IocpObjectSession_Recv::HandleIocpRecv()
// mRecvBuf[mRecvBufStart ... mRecvBufEnd)에 데이터 존재

int processedLen = OnRecv(
    &mRecvBuf[mRecvBufStart],
    mRecvBufEnd - mRecvBufStart
);

mRecvBufStart += processedLen;

if (mRecvBufStart == mRecvBufEnd)
{
    mRecvBufStart = mRecvBufEnd = 0;  // 버퍼 초기화
}
else if (mRecvBufStart > 0)
{
    // 데이터 연속성 유지 (앞으로 이동)
    memmove(mRecvBuf, &mRecvBuf[mRecvBufStart], 
            mRecvBufEnd - mRecvBufStart);
    mRecvBufEnd -= mRecvBufStart;
    mRecvBufStart = 0;
}
```

---

## 📖 권장 읽기 순서 (학습 경로)

```
Day 1:
  1. NetworkTypeDef.h (기본 개념)
  2. IocpObject.h (추상화)
  3. Doc/01_IOCP_Architecture_Analysis.md (이론)

Day 2:
  4. IocpCore.h (IOCP 관리)
  5. IocpCore.cpp (구현)
  6. IocpObjectListener.h (서버 리스너)

Day 3:
  7. ServiceCoordinator.h (서비스 관리)
  8. ServiceCoordinator.cpp (구현)

Day 4:
  9. SessionPool.h (풀 개념)
  10. IocpObjectSession.h (세션 인터페이스)
  11. IocpObjectSession.cpp (핵심 구현)

Day 5:
  12. IocpObjectSession_Send.cpp (송신 상세)
  13. IocpObjectSession_Recv.cpp (수신 상세)
  14. IocpObjectSession_SockOpt.cpp (옵션)

Day 6:
  15. SendBufferHelper.h/cpp (버퍼 래퍼)
  16. SendBufferChunkPool.h/cpp (메모리 풀)

Day 7:
  17. CryptoSession.h/cpp (선택: 암호화)
  18. IocpObjectFile.h/cpp (선택: 파일 I/O)
  19. IocpObjectJob.h/cpp (선택: Job)
```

---

## 💡 중요한 발견사항 (Key Insights)

### 1. Completion Key의 중요성
- `CreateIoCompletionPort()`의 4번째 매개변수로 IocpObject 포인터를 저장
- GQCS 반환 시 어느 객체의 완료 이벤트인지 식별 가능
- 다형성을 통해 HandleIocp() 호출

### 2. OverlappedEx의 역할
- OVERLAPPED 구조체 확장
- IO_TYPE: 작업 타입 저장 (SEND vs RECV vs ACCEPT 등)
- WSABUF: 실제 데이터 버퍼 정보
- Job*: Job 처리용

### 3. RefCount의 필요성
- std::atomic<int>로 멀티스레드 안전
- IOCP 완료 핸들러가 실행 중일 때도 세션 안전하게 해제 가능
- SessionPool의 AliveSessionSet과 함께 사용

### 4. SendBuffer 병합의 성능 이득
- WSABUF 배열: 최대 MAX_WSABUF_COUNT (보통 64)개
- 여러 개의 작은 버퍼 → 하나의 WSASend 호출
- 시스템 콜 오버헤드 감소
- 처리량 증가

### 5. RecvBuffer의 연속성 유지
- mRecvBufStart / mRecvBufEnd로 유효 범위 추적
- 부분 처리 시 memmove로 앞으로 이동
- 버퍼 재할당 없이 메모리 효율적

### 6. ServiceType의 유연성
- SERVER: AcceptEx로 클라이언트 수락
- CLIENT: ConnectEx로 수동 연결
- CLIENT_AUTO_RECONNECT: 자동 재연결
- OFFLINE_MODE: 오프라인 처리

---

## 🔗 상호 참조 맵

```
IocpCore
  ├── ServiceCoordinator (1:many)
  │   ├── SessionPool (1:1)
  │   │   └── IocpObjectSession (many:1)
  │   │       ├── SendBufferQueue (1:1)
  │   │       ├── RecvBuffer (1:1)
  │   │       └── RefCount (1:1)
  │   └── IocpObjectListener (0..1:1, SERVER 전용)
  │       ├── Accept (1:1)
  │       └── SessionCount (1:1)
  
OverlappedEx
  ├── IO_TYPE (enum)
  ├── WSABUF (Winsock)
  └── Job* (Job 처리)

SendBuffer
  ├── SendBufferChunk[]
  ├── SendBufferChunkPool (메모리 재사용)
  └── SendBufferHelper (RAII)
```

---

## 📌 체크리스트: 파일 읽기 완료

```
필수 (Tier 1-2):
☐ NetworkTypeDef.h
☐ IocpObject.h
☐ IocpCore.h/cpp
☐ IocpObjectSession.h/cpp
☐ IocpObjectListener.h/cpp
☐ ServiceCoordinator.h/cpp
☐ SessionPool.h/cpp

권장 (Tier 3):
☐ IocpObjectSession_Send.cpp
☐ IocpObjectSession_Recv.cpp
☐ IocpObjectSession_SockOpt.cpp

선택 (Tier 4):
☐ SendBufferHelper.h/cpp
☐ SendBufferChunkPool.h/cpp
☐ IocpObjectJob.h/cpp
☐ IocpObjectFile.h/cpp
☐ CryptoSession.h/cpp
```

---

**생성일**: 2026-01-27  
**최종 수정**: 2026-01-27  
**상태**: 완성 ✓

