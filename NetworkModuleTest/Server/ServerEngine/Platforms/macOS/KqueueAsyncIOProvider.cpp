// English: kqueue-based AsyncIOProvider implementation for macOS/BSD
// 한글: macOS/BSD용 kqueue 기반 AsyncIOProvider 구현

#ifdef __APPLE__

#include "KqueueAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <unistd.h>
#include <cstring>

namespace Network::AsyncIO::BSD
{
    // =============================================================================
    // English: Constructor & Destructor
    // 한글: 생성자 및 소멸자
    // =============================================================================

    KqueueAsyncIOProvider::KqueueAsyncIOProvider()
        : mKqueueFd(-1)
        , mInfo{}
        , mStats{}
        , mMaxConcurrentOps(0)
        , mInitialized(false)
    {
    }

    KqueueAsyncIOProvider::~KqueueAsyncIOProvider()
    {
        Shutdown();
    }

    // =============================================================================
    // English: Lifecycle Management
    // 한글: 생명주기 관리
    // =============================================================================

    AsyncIOError KqueueAsyncIOProvider::Initialize(size_t queueDepth, size_t maxConcurrent)
    {
        if (mInitialized)
            return AsyncIOError::AlreadyInitialized;

        // English: Create kqueue file descriptor
        // 한글: kqueue 파일 디스크립터 생성
        mKqueueFd = kqueue();
        if (mKqueueFd < 0)
        {
            mLastError = "kqueue() failed";
            return AsyncIOError::OperationFailed;
        }

        mMaxConcurrentOps = maxConcurrent;

        // English: Initialize provider info
        // 한글: 공급자 정보 초기화
        mInfo.mPlatformType = PlatformType::Kqueue;
        mInfo.mName = "kqueue";
        mInfo.mMaxQueueDepth = queueDepth;
        mInfo.mMaxConcurrentReq = maxConcurrent;
        mInfo.mSupportsBufferReg = false;
        mInfo.mSupportsBatching = false;
        mInfo.mSupportsZeroCopy = false;

        mInitialized = true;
        return AsyncIOError::Success;
    }

    void KqueueAsyncIOProvider::Shutdown()
    {
        if (!mInitialized)
            return;

        std::lock_guard<std::mutex> lock(mMutex);

        // English: Close kqueue file descriptor
        // 한글: kqueue 파일 디스크립터 닫기
        if (mKqueueFd >= 0)
        {
            close(mKqueueFd);
            mKqueueFd = -1;
        }

        mPendingOps.clear();
        mRegisteredSockets.clear();
        mInitialized = false;
    }

    bool KqueueAsyncIOProvider::IsInitialized() const
    {
        return mInitialized;
    }

    // =============================================================================
    // English: Buffer Management
    // 한글: 버퍼 관리
    // =============================================================================

    int64_t KqueueAsyncIOProvider::RegisterBuffer(const void* ptr, size_t size)
    {
        // English: kqueue doesn't support pre-registered buffers (no-op)
        // 한글: kqueue는 사전 등록 버퍼를 지원하지 않음 (no-op)
        return -1;
    }

    AsyncIOError KqueueAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
    {
        return AsyncIOError::PlatformNotSupported;
    }

    // =============================================================================
    // English: Async I/O Operations
    // 한글: 비동기 I/O 작업
    // =============================================================================

    AsyncIOError KqueueAsyncIOProvider::SendAsync(
        SocketHandle socket,
        const void* buffer,
        size_t size,
        RequestContext context,
        uint32_t flags
    )
    {
        if (!mInitialized)
            return AsyncIOError::NotInitialized;
        if (socket < 0 || !buffer || size == 0)
            return AsyncIOError::InvalidParameter;

        std::lock_guard<std::mutex> lock(mMutex);

        // English: Store pending operation with buffer copy
        // 한글: 버퍼 복사와 함께 대기 작업 저장
        PendingOperation pending;
        pending.mContext = context;
        pending.mType = AsyncIOType::Send;
        pending.mSocket = socket;
        pending.mBuffer = std::make_unique<uint8_t[]>(size);
        std::memcpy(pending.mBuffer.get(), buffer, size);
        pending.mBufferSize = static_cast<uint32_t>(size);

        mPendingOps[socket] = std::move(pending);
        mStats.mTotalRequests++;
        mStats.mPendingRequests++;

        return AsyncIOError::Success;
    }

    AsyncIOError KqueueAsyncIOProvider::RecvAsync(
        SocketHandle socket,
        void* buffer,
        size_t size,
        RequestContext context,
        uint32_t flags
    )
    {
        if (!mInitialized)
            return AsyncIOError::NotInitialized;
        if (socket < 0 || !buffer || size == 0)
            return AsyncIOError::InvalidParameter;

        std::lock_guard<std::mutex> lock(mMutex);

        PendingOperation pending;
        pending.mContext = context;
        pending.mType = AsyncIOType::Recv;
        pending.mSocket = socket;
        pending.mBuffer = nullptr;
        pending.mBufferSize = static_cast<uint32_t>(size);

        mPendingOps[socket] = std::move(pending);
        mStats.mTotalRequests++;
        mStats.mPendingRequests++;

        return AsyncIOError::Success;
    }

    AsyncIOError KqueueAsyncIOProvider::FlushRequests()
    {
        // English: kqueue doesn't support batch processing (no-op)
        // 한글: kqueue는 배치 처리를 지원하지 않음 (no-op)
        if (!mInitialized)
            return AsyncIOError::NotInitialized;

        return AsyncIOError::Success;
    }

    // =============================================================================
    // English: Completion Processing
    // 한글: 완료 처리
    // =============================================================================

    int KqueueAsyncIOProvider::ProcessCompletions(
        CompletionEntry* entries,
        size_t maxEntries,
        int timeoutMs
    )
    {
        if (!mInitialized)
            return static_cast<int>(AsyncIOError::NotInitialized);
        if (!entries || maxEntries == 0 || mKqueueFd < 0)
            return static_cast<int>(AsyncIOError::InvalidParameter);

        // English: Prepare timeout structure
        // 한글: 타임아웃 구조체 준비
        struct timespec ts;
        struct timespec* pts = nullptr;

        if (timeoutMs >= 0)
        {
            ts.tv_sec = timeoutMs / 1000;
            ts.tv_nsec = (timeoutMs % 1000) * 1000000;
            pts = &ts;
        }

        // English: Poll for events
        // 한글: 이벤트 폴링
        std::unique_ptr<struct kevent[]> events(new struct kevent[maxEntries]);
        int numEvents = kevent(mKqueueFd, nullptr, 0, events.get(), static_cast<int>(maxEntries), pts);

        if (numEvents <= 0)
            return 0;

        int processedCount = 0;

        for (int i = 0; i < numEvents && processedCount < static_cast<int>(maxEntries); ++i)
        {
            struct kevent& event = events[i];
            SocketHandle socket = static_cast<SocketHandle>(event.ident);

            std::lock_guard<std::mutex> lock(mMutex);
            auto it = mPendingOps.find(socket);
            if (it != mPendingOps.end())
            {
                // English: Match event type with operation type
                // 한글: 이벤트 타입과 작업 타입 매칭
                if ((event.filter == EVFILT_READ && it->second.mType == AsyncIOType::Recv) ||
                    (event.filter == EVFILT_WRITE && it->second.mType == AsyncIOType::Send))
                {
                    CompletionEntry& entry = entries[processedCount];
                    entry.mContext = it->second.mContext;
                    entry.mType = it->second.mType;
                    entry.mResult = (event.data > 0)
                        ? static_cast<int32_t>(event.data)
                        : static_cast<int32_t>(it->second.mBufferSize);
                    entry.mOsError = (event.flags & EV_ERROR) ? static_cast<OSError>(event.data) : 0;
                    entry.mCompletionTime = 0;

                    mPendingOps.erase(it);
                    mStats.mPendingRequests--;
                    mStats.mTotalCompletions++;
                    processedCount++;
                }
            }
        }

        return processedCount;
    }

    // =============================================================================
    // English: Helper Methods
    // 한글: 헬퍼 메서드
    // =============================================================================

    bool KqueueAsyncIOProvider::RegisterSocketEvents(SocketHandle socket)
    {
        // English: Register for both read and write events
        // 한글: 읽기 및 쓰기 이벤트 등록
        struct kevent events[2];
        EV_SET(&events[0], socket, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        EV_SET(&events[1], socket, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);

        return kevent(mKqueueFd, events, 2, nullptr, 0, nullptr) >= 0;
    }

    bool KqueueAsyncIOProvider::UnregisterSocketEvents(SocketHandle socket)
    {
        // English: Delete read and write events
        // 한글: 읽기 및 쓰기 이벤트 삭제
        struct kevent events[2];
        EV_SET(&events[0], socket, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        EV_SET(&events[1], socket, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

        // English: Ignore errors (socket might already be closed)
        // 한글: 에러 무시 (소켓이 이미 닫혔을 수 있음)
        kevent(mKqueueFd, events, 2, nullptr, 0, nullptr);
        return true;
    }

    // =============================================================================
    // English: Information & Statistics
    // 한글: 정보 및 통계
    // =============================================================================

    const ProviderInfo& KqueueAsyncIOProvider::GetInfo() const
    {
        return mInfo;
    }

    ProviderStats KqueueAsyncIOProvider::GetStats() const
    {
        return mStats;
    }

    const char* KqueueAsyncIOProvider::GetLastError() const
    {
        return mLastError.c_str();
    }

    // =============================================================================
    // English: Factory Function
    // 한글: 팩토리 함수
    // =============================================================================

    std::unique_ptr<AsyncIOProvider> CreateKqueueProvider()
    {
        return std::make_unique<KqueueAsyncIOProvider>();
    }

}  // namespace Network::AsyncIO::BSD

#endif  // __APPLE__
