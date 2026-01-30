// English: RIO (Registered I/O) based AsyncIOProvider implementation
// 한글: RIO (등록 I/O) 기반 AsyncIOProvider 구현

#ifdef _WIN32

#include "RIOAsyncIOProvider.h"
#include "Network/Core/PlatformDetect.h"
#include <cstring>

namespace Network::AsyncIO::Windows
{
    // =============================================================================
    // English: Constructor & Destructor
    // 한글: 생성자 및 소멸자
    // =============================================================================

    RIOAsyncIOProvider::RIOAsyncIOProvider()
        : mCompletionQueue(RIO_INVALID_CQ)
        , mInfo{}
        , mStats{}
        , mMaxConcurrentOps(0)
        , mNextBufferId(1)
        , mInitialized(false)
        , mPfnRIOCloseCompletionQueue(nullptr)
        , mPfnRIOCreateCompletionQueue(nullptr)
        , mPfnRIOCreateRequestQueue(nullptr)
        , mPfnRIODequeueCompletion(nullptr)
        , mPfnRIONotify(nullptr)
        , mPfnRIORegisterBuffer(nullptr)
        , mPfnRIODeregisterBuffer(nullptr)
        , mPfnRIOSend(nullptr)
        , mPfnRIORecv(nullptr)
    {
    }

    RIOAsyncIOProvider::~RIOAsyncIOProvider()
    {
        // English: Ensure resources are released
        // 한글: 리소스 해제 보장
        Shutdown();
    }

    // =============================================================================
    // English: Lifecycle Management
    // 한글: 생명주기 관리
    // =============================================================================

    AsyncIOError RIOAsyncIOProvider::Initialize(size_t queueDepth, size_t maxConcurrent)
    {
        // English: Check if already initialized
        // 한글: 이미 초기화되었는지 확인
        if (mInitialized)
            return AsyncIOError::AlreadyInitialized;

        // English: Load RIO functions from mswsock.dll
        // 한글: mswsock.dll에서 RIO 함수 로드
        if (!LoadRIOFunctions())
        {
            mLastError = "Failed to load RIO functions";
            return AsyncIOError::PlatformNotSupported;
        }

        // English: Create completion queue
        // 한글: 완료 큐 생성
        mCompletionQueue = mPfnRIOCreateCompletionQueue(static_cast<DWORD>(queueDepth), nullptr);
        if (mCompletionQueue == RIO_INVALID_CQ)
        {
            mLastError = "RIOCreateCompletionQueue failed";
            return AsyncIOError::OperationFailed;
        }

        // English: Store configuration
        // 한글: 설정 저장
        mMaxConcurrentOps = maxConcurrent;

        // English: Initialize provider info
        // 한글: 공급자 정보 초기화
        mInfo.mPlatformType = PlatformType::RIO;
        mInfo.mName = "RIO";
        mInfo.mMaxQueueDepth = queueDepth;
        mInfo.mMaxConcurrentReq = maxConcurrent;
        mInfo.mSupportsBufferReg = true;
        mInfo.mSupportsBatching = true;
        mInfo.mSupportsZeroCopy = true;

        mInitialized = true;
        return AsyncIOError::Success;
    }

    void RIOAsyncIOProvider::Shutdown()
    {
        // English: Skip if not initialized
        // 한글: 초기화되지 않았으면 건너뜀
        if (!mInitialized)
            return;

        std::lock_guard<std::mutex> lock(mMutex);

        // English: Close all request queues
        // 한글: 모든 요청 큐 닫기
        mRequestQueues.clear();

        // English: Deregister all buffers
        // 한글: 모든 버퍼 등록 해제
        for (auto& pair : mRegisteredBuffers)
        {
            if (mPfnRIODeregisterBuffer)
                mPfnRIODeregisterBuffer(pair.second.mRioBufferId);
        }
        mRegisteredBuffers.clear();

        // English: Close completion queue
        // 한글: 완료 큐 닫기
        if (mCompletionQueue != RIO_INVALID_CQ && mPfnRIOCloseCompletionQueue)
        {
            mPfnRIOCloseCompletionQueue(mCompletionQueue);
            mCompletionQueue = RIO_INVALID_CQ;
        }

        mInitialized = false;
    }

    bool RIOAsyncIOProvider::IsInitialized() const
    {
        return mInitialized;
    }

    // =============================================================================
    // English: Buffer Management
    // 한글: 버퍼 관리
    // =============================================================================

    int64_t RIOAsyncIOProvider::RegisterBuffer(const void* ptr, size_t size)
    {
        // English: Validate parameters
        // 한글: 매개변수 유효성 검사
        if (!mInitialized || !ptr || size == 0 || !mPfnRIORegisterBuffer)
            return -1;

        std::lock_guard<std::mutex> lock(mMutex);

        // English: Register buffer with RIO
        // 한글: RIO에 버퍼 등록
        RIO_BUFFERID rioBufferId = mPfnRIORegisterBuffer(
            const_cast<PCHAR>(static_cast<const char*>(ptr)),
            static_cast<DWORD>(size)
        );
        if (rioBufferId == RIO_INVALID_BUFFERID)
            return -1;

        // English: Store registration
        // 한글: 등록 정보 저장
        int64_t bufferId = mNextBufferId++;
        mRegisteredBuffers[bufferId] = {rioBufferId, const_cast<void*>(ptr), static_cast<uint32_t>(size)};

        return bufferId;
    }

    AsyncIOError RIOAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
    {
        // English: Validate state
        // 한글: 상태 유효성 검사
        if (!mInitialized || !mPfnRIODeregisterBuffer)
            return AsyncIOError::NotInitialized;

        std::lock_guard<std::mutex> lock(mMutex);

        // English: Find and deregister buffer
        // 한글: 버퍼 찾기 및 등록 해제
        auto it = mRegisteredBuffers.find(bufferId);
        if (it == mRegisteredBuffers.end())
            return AsyncIOError::InvalidBuffer;

        mPfnRIODeregisterBuffer(it->second.mRioBufferId);
        mRegisteredBuffers.erase(it);
        return AsyncIOError::Success;
    }

    // =============================================================================
    // English: Async I/O Operations
    // 한글: 비동기 I/O 작업
    // =============================================================================

    AsyncIOError RIOAsyncIOProvider::SendAsync(
        SocketHandle socket,
        const void* buffer,
        size_t size,
        RequestContext context,
        uint32_t flags
    )
    {
        // English: RIO requires pre-registered buffers for sends
        // 한글: RIO는 송신에 사전 등록된 버퍼가 필요
        if (!mInitialized)
            return AsyncIOError::NotInitialized;

        // English: Non-registered buffer send not supported in RIO
        // 한글: RIO에서는 미등록 버퍼 송신이 지원되지 않음
        mLastError = "RIO requires pre-registered buffers; use RegisterBuffer first";
        return AsyncIOError::InvalidBuffer;
    }

    AsyncIOError RIOAsyncIOProvider::RecvAsync(
        SocketHandle socket,
        void* buffer,
        size_t size,
        RequestContext context,
        uint32_t flags
    )
    {
        // English: RIO requires pre-registered buffers for receives
        // 한글: RIO는 수신에 사전 등록된 버퍼가 필요
        if (!mInitialized)
            return AsyncIOError::NotInitialized;

        // English: Non-registered buffer recv not supported in RIO
        // 한글: RIO에서는 미등록 버퍼 수신이 지원되지 않음
        mLastError = "RIO requires pre-registered buffers; use RegisterBuffer first";
        return AsyncIOError::InvalidBuffer;
    }

    AsyncIOError RIOAsyncIOProvider::FlushRequests()
    {
        // English: Commit all deferred sends to kernel
        // 한글: 모든 지연된 송신을 커널에 커밋
        if (!mInitialized)
            return AsyncIOError::NotInitialized;

        // English: Note: In full implementation, call RIOCommitSends for each request queue
        // 한글: 참고: 전체 구현에서는 각 요청 큐에 대해 RIOCommitSends 호출
        return AsyncIOError::Success;
    }

    // =============================================================================
    // English: Completion Processing
    // 한글: 완료 처리
    // =============================================================================

    int RIOAsyncIOProvider::ProcessCompletions(
        CompletionEntry* entries,
        size_t maxEntries,
        int timeoutMs
    )
    {
        // English: Validate parameters
        // 한글: 매개변수 유효성 검사
        if (!mInitialized || !mPfnRIODequeueCompletion)
            return static_cast<int>(AsyncIOError::NotInitialized);
        if (!entries || maxEntries == 0)
            return static_cast<int>(AsyncIOError::InvalidParameter);

        std::lock_guard<std::mutex> lock(mMutex);

        // English: Allocate temporary buffer for RIO results
        // 한글: RIO 결과용 임시 버퍼 할당
        std::unique_ptr<RIORESULT[]> rioResults(new RIORESULT[maxEntries]);

        // English: Dequeue completions from RIO
        // 한글: RIO에서 완료 항목 추출
        ULONG completionCount = mPfnRIODequeueCompletion(
            mCompletionQueue,
            rioResults.get(),
            static_cast<ULONG>(maxEntries)
        );

        if (completionCount == RIO_CORRUPT_CQ)
        {
            mLastError = "RIO completion queue is corrupt";
            return static_cast<int>(AsyncIOError::OperationFailed);
        }

        // English: Convert RIO results to CompletionEntry
        // 한글: RIO 결과를 CompletionEntry로 변환
        for (ULONG i = 0; i < completionCount; ++i)
        {
            entries[i].mResult = static_cast<int32_t>(rioResults[i].BytesTransferred);
            entries[i].mOsError = rioResults[i].Status;
            entries[i].mCompletionTime = 0;

            // English: Look up pending operation
            // 한글: 대기 작업 조회
            void* requestContext = reinterpret_cast<void*>(rioResults[i].RequestContext);
            auto opIt = mPendingOps.find(requestContext);
            if (opIt != mPendingOps.end())
            {
                entries[i].mContext = opIt->second.mContext;
                entries[i].mType = opIt->second.mType;
                mPendingOps.erase(opIt);
            }

            mStats.mTotalCompletions++;
        }

        mStats.mPendingRequests -= completionCount;
        return static_cast<int>(completionCount);
    }

    // =============================================================================
    // English: Information & Statistics
    // 한글: 정보 및 통계
    // =============================================================================

    const ProviderInfo& RIOAsyncIOProvider::GetInfo() const
    {
        return mInfo;
    }

    ProviderStats RIOAsyncIOProvider::GetStats() const
    {
        return mStats;
    }

    const char* RIOAsyncIOProvider::GetLastError() const
    {
        return mLastError.c_str();
    }

    // =============================================================================
    // English: Helper Methods
    // 한글: 헬퍼 메서드
    // =============================================================================

    bool RIOAsyncIOProvider::LoadRIOFunctions()
    {
        // English: Load mswsock.dll for RIO functions
        // 한글: RIO 함수를 위해 mswsock.dll 로드
        HMODULE hMswsock = LoadLibraryA("mswsock.dll");
        if (!hMswsock)
            return false;

        // English: Load RIO function pointers
        // 한글: RIO 함수 포인터 로드
        mPfnRIOCloseCompletionQueue = (PfnRIOCloseCompletionQueue)GetProcAddress(hMswsock, "RIOCloseCompletionQueue");
        mPfnRIOCreateCompletionQueue = (PfnRIOCreateCompletionQueue)GetProcAddress(hMswsock, "RIOCreateCompletionQueue");
        mPfnRIOCreateRequestQueue = (PfnRIOCreateRequestQueue)GetProcAddress(hMswsock, "RIOCreateRequestQueue");
        mPfnRIODequeueCompletion = (PfnRIODequeueCompletion)GetProcAddress(hMswsock, "RIODequeueCompletion");
        mPfnRIONotify = (PfnRIONotify)GetProcAddress(hMswsock, "RIONotify");
        mPfnRIORegisterBuffer = (PfnRIORegisterBuffer)GetProcAddress(hMswsock, "RIORegisterBuffer");
        mPfnRIODeregisterBuffer = (PfnRIODeregisterBuffer)GetProcAddress(hMswsock, "RIODeregisterBuffer");
        mPfnRIOSend = (PfnRIOSend)GetProcAddress(hMswsock, "RIOSend");
        mPfnRIORecv = (PfnRIORecv)GetProcAddress(hMswsock, "RIORecv");

        // English: Check if all required functions loaded
        // 한글: 필수 함수가 모두 로드되었는지 확인
        return (mPfnRIOCloseCompletionQueue && mPfnRIOCreateCompletionQueue &&
                mPfnRIOCreateRequestQueue && mPfnRIODequeueCompletion &&
                mPfnRIORegisterBuffer && mPfnRIODeregisterBuffer &&
                mPfnRIOSend && mPfnRIORecv);
    }

    // =============================================================================
    // English: Factory Function
    // 한글: 팩토리 함수
    // =============================================================================

    std::unique_ptr<AsyncIOProvider> CreateRIOProvider()
    {
        return std::make_unique<RIOAsyncIOProvider>();
    }

}  // namespace Network::AsyncIO::Windows

#endif  // _WIN32
