// English: IOCP-based AsyncIOProvider implementation
// 한글: IOCP 기반 AsyncIOProvider 구현

#ifdef _WIN32

#include "IocpAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstring>

namespace Network::AsyncIO::Windows
{
    // =============================================================================
    // English: Constructor & Destructor
    // 한글: 생성자 및 소멸자
    // =============================================================================

    IocpAsyncIOProvider::IocpAsyncIOProvider()
        : mCompletionPort(INVALID_HANDLE_VALUE)
        , mInfo{}
        , mStats{}
        , mMaxConcurrentOps(0)
        , mInitialized(false)
    {
    }

    IocpAsyncIOProvider::~IocpAsyncIOProvider()
    {
        // English: Ensure resources are released
        // 한글: 리소스 해제 보장
        Shutdown();
    }

    // =============================================================================
    // English: Lifecycle Management
    // 한글: 생명주기 관리
    // =============================================================================

    AsyncIOError IocpAsyncIOProvider::Initialize(size_t queueDepth, size_t maxConcurrent)
    {
        // English: Check if already initialized
        // 한글: 이미 초기화되었는지 확인
        if (mInitialized)
            return AsyncIOError::AlreadyInitialized;

        // English: Create IOCP handle
        // 한글: IOCP 핸들 생성
        mCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (mCompletionPort == INVALID_HANDLE_VALUE)
        {
            mLastError = "CreateIoCompletionPort failed";
            return AsyncIOError::OperationFailed;
        }

        // English: Store configuration
        // 한글: 설정 저장
        mMaxConcurrentOps = maxConcurrent;

        // English: Initialize provider info
        // 한글: 공급자 정보 초기화
        mInfo.mPlatformType = PlatformType::IOCP;
        mInfo.mName = "IOCP";
        mInfo.mMaxQueueDepth = queueDepth;
        mInfo.mMaxConcurrentReq = maxConcurrent;
        mInfo.mSupportsBufferReg = false;
        mInfo.mSupportsBatching = false;
        mInfo.mSupportsZeroCopy = false;

        mInitialized = true;
        return AsyncIOError::Success;
    }

    void IocpAsyncIOProvider::Shutdown()
    {
        // English: Skip if not initialized
        // 한글: 초기화되지 않았으면 건너뜀
        if (!mInitialized)
            return;

        // English: Close all pending operations
        // 한글: 모든 대기 작업 닫기
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mPendingOps.clear();
        }

        // English: Close IOCP handle
        // 한글: IOCP 핸들 닫기
        if (mCompletionPort != INVALID_HANDLE_VALUE)
        {
            CloseHandle(mCompletionPort);
            mCompletionPort = INVALID_HANDLE_VALUE;
        }

        mInitialized = false;
    }

    bool IocpAsyncIOProvider::IsInitialized() const
    {
        return mInitialized;
    }

    // =============================================================================
    // English: Buffer Management
    // 한글: 버퍼 관리
    // =============================================================================

    int64_t IocpAsyncIOProvider::RegisterBuffer(const void* ptr, size_t size)
    {
        // English: IOCP doesn't support pre-registered buffers (no-op)
        // 한글: IOCP는 사전 등록 버퍼를 지원하지 않음 (no-op)
        return -1;
    }

    AsyncIOError IocpAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
    {
        // English: Not supported on IOCP
        // 한글: IOCP에서 지원하지 않음
        return AsyncIOError::PlatformNotSupported;
    }

    // =============================================================================
    // English: Async I/O Operations
    // 한글: 비동기 I/O 작업
    // =============================================================================

    AsyncIOError IocpAsyncIOProvider::SendAsync(
        SocketHandle socket,
        const void* buffer,
        size_t size,
        RequestContext context,
        uint32_t flags
    )
    {
        // English: Validate parameters
        // 한글: 매개변수 유효성 검사
        if (!mInitialized)
            return AsyncIOError::NotInitialized;
        if (socket == INVALID_SOCKET || !buffer || size == 0)
            return AsyncIOError::InvalidParameter;

        std::lock_guard<std::mutex> lock(mMutex);

        // English: Create pending operation
        // 한글: 대기 작업 생성
        auto pendingOp = std::make_unique<PendingOperation>();
        pendingOp->mContext = context;
        pendingOp->mType = AsyncIOType::Send;

        // English: Copy data to internal buffer
        // 한글: 내부 버퍼에 데이터 복사
        pendingOp->mBuffer = std::make_unique<uint8_t[]>(size);
        std::memcpy(pendingOp->mBuffer.get(), buffer, size);

        // English: Setup WSABUF
        // 한글: WSABUF 설정
        pendingOp->mWsaBuffer.buf = reinterpret_cast<char*>(pendingOp->mBuffer.get());
        pendingOp->mWsaBuffer.len = static_cast<ULONG>(size);

        // English: Initialize OVERLAPPED
        // 한글: OVERLAPPED 초기화
        std::memset(&pendingOp->mOverlapped, 0, sizeof(OVERLAPPED));

        // English: Issue WSASend
        // 한글: WSASend 호출
        DWORD bytesSent = 0;
        int result = WSASend(
            socket,
            &pendingOp->mWsaBuffer,
            1,
            &bytesSent,
            flags,
            &pendingOp->mOverlapped,
            nullptr
        );

        if (result == SOCKET_ERROR)
        {
            DWORD error = WSAGetLastError();
            if (error != WSA_IO_PENDING)
            {
                mLastError = "WSASend failed";
                mStats.mErrorCount++;
                return AsyncIOError::OperationFailed;
            }
        }

        // English: Store pending operation
        // 한글: 대기 작업 저장
        mPendingOps[socket] = std::move(pendingOp);
        mStats.mTotalRequests++;
        mStats.mPendingRequests++;

        return AsyncIOError::Success;
    }

    AsyncIOError IocpAsyncIOProvider::RecvAsync(
        SocketHandle socket,
        void* buffer,
        size_t size,
        RequestContext context,
        uint32_t flags
    )
    {
        // English: Validate parameters
        // 한글: 매개변수 유효성 검사
        if (!mInitialized)
            return AsyncIOError::NotInitialized;
        if (socket == INVALID_SOCKET || !buffer || size == 0)
            return AsyncIOError::InvalidParameter;

        std::lock_guard<std::mutex> lock(mMutex);

        // English: Create pending operation
        // 한글: 대기 작업 생성
        auto pendingOp = std::make_unique<PendingOperation>();
        pendingOp->mContext = context;
        pendingOp->mType = AsyncIOType::Recv;

        // English: Setup WSABUF (use provided buffer directly for recv)
        // 한글: WSABUF 설정 (수신용으로 제공된 버퍼를 직접 사용)
        pendingOp->mWsaBuffer.buf = static_cast<char*>(buffer);
        pendingOp->mWsaBuffer.len = static_cast<ULONG>(size);

        // English: Initialize OVERLAPPED
        // 한글: OVERLAPPED 초기화
        std::memset(&pendingOp->mOverlapped, 0, sizeof(OVERLAPPED));

        // English: Issue WSARecv
        // 한글: WSARecv 호출
        DWORD bytesRecvd = 0;
        DWORD recvFlags = flags;
        int result = WSARecv(
            socket,
            &pendingOp->mWsaBuffer,
            1,
            &bytesRecvd,
            &recvFlags,
            &pendingOp->mOverlapped,
            nullptr
        );

        if (result == SOCKET_ERROR)
        {
            DWORD error = WSAGetLastError();
            if (error != WSA_IO_PENDING)
            {
                mLastError = "WSARecv failed";
                mStats.mErrorCount++;
                return AsyncIOError::OperationFailed;
            }
        }

        // English: Store pending operation
        // 한글: 대기 작업 저장
        mPendingOps[socket] = std::move(pendingOp);
        mStats.mTotalRequests++;
        mStats.mPendingRequests++;

        return AsyncIOError::Success;
    }

    AsyncIOError IocpAsyncIOProvider::FlushRequests()
    {
        // English: IOCP doesn't support batch processing (no-op)
        // 한글: IOCP는 배치 처리를 지원하지 않음 (no-op)
        if (!mInitialized)
            return AsyncIOError::NotInitialized;

        return AsyncIOError::Success;
    }

    // =============================================================================
    // English: Completion Processing
    // 한글: 완료 처리
    // =============================================================================

    int IocpAsyncIOProvider::ProcessCompletions(
        CompletionEntry* entries,
        size_t maxEntries,
        int timeoutMs
    )
    {
        // English: Validate parameters
        // 한글: 매개변수 유효성 검사
        if (!mInitialized)
            return static_cast<int>(AsyncIOError::NotInitialized);
        if (!entries || maxEntries == 0)
            return static_cast<int>(AsyncIOError::InvalidParameter);

        int processedCount = 0;
        DWORD timeout = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);

        for (size_t i = 0; i < maxEntries; ++i)
        {
            DWORD bytesTransferred = 0;
            ULONG_PTR completionKey = 0;
            LPOVERLAPPED pOverlapped = nullptr;

            // English: Use timeout only on first iteration
            // 한글: 첫 번째 반복에서만 타임아웃 사용
            BOOL success = GetQueuedCompletionStatus(
                mCompletionPort,
                &bytesTransferred,
                &completionKey,
                &pOverlapped,
                (i == 0) ? timeout : 0
            );

            if (!success && pOverlapped == nullptr)
            {
                // English: No more completions available
                // 한글: 더 이상 완료 작업 없음
                break;
            }

            // English: Process this completion
            // 한글: 이 완료 작업 처리
            if (pOverlapped != nullptr)
            {
                CompletionEntry& entry = entries[processedCount];
                entry.mResult = static_cast<int32_t>(bytesTransferred);
                entry.mOsError = success ? 0 : ::GetLastError();
                entry.mCompletionTime = 0;

                // English: Look up pending operation to get context and type
                // 한글: 컨텍스트와 타입을 위해 대기 작업 조회
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    auto it = mPendingOps.find(static_cast<SocketHandle>(completionKey));
                    if (it != mPendingOps.end())
                    {
                        entry.mContext = it->second->mContext;
                        entry.mType = it->second->mType;
                        mPendingOps.erase(it);
                        mStats.mPendingRequests--;
                    }
                }

                processedCount++;
                mStats.mTotalCompletions++;
            }
        }

        return processedCount;
    }

    // =============================================================================
    // English: Information & Statistics
    // 한글: 정보 및 통계
    // =============================================================================

    const ProviderInfo& IocpAsyncIOProvider::GetInfo() const
    {
        return mInfo;
    }

    ProviderStats IocpAsyncIOProvider::GetStats() const
    {
        return mStats;
    }

    const char* IocpAsyncIOProvider::GetLastError() const
    {
        return mLastError.c_str();
    }

    // =============================================================================
    // English: Factory Function
    // 한글: 팩토리 함수
    // =============================================================================

    std::unique_ptr<AsyncIOProvider> CreateIocpProvider()
    {
        return std::make_unique<IocpAsyncIOProvider>();
    }

}  // namespace Network::AsyncIO::Windows

#endif  // _WIN32
