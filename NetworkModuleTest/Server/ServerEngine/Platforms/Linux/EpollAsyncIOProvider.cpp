// English: epoll-based AsyncIOProvider implementation
// 한글: epoll 기반 AsyncIOProvider 구현

#ifdef __linux__

#include "EpollAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>

namespace Network {
namespace AsyncIO {
namespace Linux {
    // =============================================================================
    // English: Constructor & Destructor
    // 한글: 생성자 및 소멸자
    // =============================================================================

    EpollAsyncIOProvider::EpollAsyncIOProvider()
        : mEpollFd(-1)
        , mInfo{}
        , mStats{}
        , mMaxConcurrentOps(0)
        , mInitialized(false)
    {
    }

    EpollAsyncIOProvider::~EpollAsyncIOProvider()
    {
        // English: Ensure resources are released
        // 한글: 리소스 해제 보장
        Shutdown();
    }

    // =============================================================================
    // English: Lifecycle Management
    // 한글: 생명주기 관리
    // =============================================================================

    AsyncIOError EpollAsyncIOProvider::Initialize(size_t queueDepth, size_t maxConcurrent)
    {
        if (mInitialized)
            return AsyncIOError::AlreadyInitialized;

        // English: Create epoll file descriptor with close-on-exec
        // 한글: close-on-exec로 epoll 파일 디스크립터 생성
        mEpollFd = epoll_create1(EPOLL_CLOEXEC);
        if (mEpollFd < 0)
        {
            mLastError = "epoll_create1 failed";
            return AsyncIOError::OperationFailed;
        }

        mMaxConcurrentOps = maxConcurrent;

        // English: Initialize provider info
        // 한글: 공급자 정보 초기화
        mInfo.mPlatformType = PlatformType::Epoll;
        mInfo.mName = "epoll";
        mInfo.mMaxQueueDepth = queueDepth;
        mInfo.mMaxConcurrentReq = maxConcurrent;
        mInfo.mSupportsBufferReg = false;
        mInfo.mSupportsBatching = false;
        mInfo.mSupportsZeroCopy = false;

        mInitialized = true;
        return AsyncIOError::Success;
    }

    void EpollAsyncIOProvider::Shutdown()
    {
        if (!mInitialized)
            return;

        std::lock_guard<std::mutex> lock(mMutex);

        // English: Close epoll file descriptor
        // 한글: epoll 파일 디스크립터 닫기
        if (mEpollFd >= 0)
        {
            close(mEpollFd);
            mEpollFd = -1;
        }

        mPendingOps.clear();
        mInitialized = false;
    }

    bool EpollAsyncIOProvider::IsInitialized() const
    {
        return mInitialized;
    }

    // =============================================================================
    // English: Buffer Management
    // 한글: 버퍼 관리
    // =============================================================================

    int64_t EpollAsyncIOProvider::RegisterBuffer(const void* ptr, size_t size)
    {
        // English: epoll doesn't support pre-registered buffers (no-op)
        // 한글: epoll은 사전 등록 버퍼를 지원하지 않음 (no-op)
        return -1;
    }

    AsyncIOError EpollAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
    {
        // English: Not supported on epoll
        // 한글: epoll에서 지원하지 않음
        return AsyncIOError::PlatformNotSupported;
    }

    // =============================================================================
    // English: Async I/O Operations
    // 한글: 비동기 I/O 작업
    // =============================================================================

    AsyncIOError EpollAsyncIOProvider::SendAsync(
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
        pending.mBuffer = std::make_unique<uint8_t[]>(size);
        std::memcpy(pending.mBuffer.get(), buffer, size);
        pending.mBufferSize = static_cast<uint32_t>(size);

        mPendingOps[socket] = std::move(pending);
        mStats.mTotalRequests++;
        mStats.mPendingRequests++;

        return AsyncIOError::Success;
    }

    AsyncIOError EpollAsyncIOProvider::RecvAsync(
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

        // English: Store pending operation (caller manages buffer)
        // 한글: 대기 작업 저장 (호출자가 버퍼 관리)
        PendingOperation pending;
        pending.mContext = context;
        pending.mType = AsyncIOType::Recv;
        pending.mBuffer.reset();
        pending.mBufferSize = static_cast<uint32_t>(size);

        mPendingOps[socket] = std::move(pending);
        mStats.mTotalRequests++;
        mStats.mPendingRequests++;

        return AsyncIOError::Success;
    }

    AsyncIOError EpollAsyncIOProvider::FlushRequests()
    {
        // English: epoll doesn't support batch processing (no-op)
        // 한글: epoll은 배치 처리를 지원하지 않음 (no-op)
        if (!mInitialized)
            return AsyncIOError::NotInitialized;

        return AsyncIOError::Success;
    }

    // =============================================================================
    // English: Completion Processing
    // 한글: 완료 처리
    // =============================================================================

    int EpollAsyncIOProvider::ProcessCompletions(
        CompletionEntry* entries,
        size_t maxEntries,
        int timeoutMs
    )
    {
        if (!mInitialized)
            return static_cast<int>(AsyncIOError::NotInitialized);
        if (!entries || maxEntries == 0 || mEpollFd < 0)
            return static_cast<int>(AsyncIOError::InvalidParameter);

        // English: Poll for events
        // 한글: 이벤트 폴링
        std::unique_ptr<struct epoll_event[]> events(new struct epoll_event[maxEntries]);
        int numEvents = epoll_wait(mEpollFd, events.get(), static_cast<int>(maxEntries), timeoutMs);

        if (numEvents < 0)
        {
            mLastError = "epoll_wait failed";
            return static_cast<int>(AsyncIOError::OperationFailed);
        }

        if (numEvents == 0)
            return 0;

        int processedCount = 0;

        for (int i = 0; i < numEvents && processedCount < static_cast<int>(maxEntries); ++i)
        {
            SocketHandle socket = events[i].data.fd;

            std::lock_guard<std::mutex> lock(mMutex);
            auto it = mPendingOps.find(socket);
            if (it != mPendingOps.end())
            {
                // English: Fill completion entry
                // 한글: 완료 항목 채우기
                CompletionEntry& entry = entries[processedCount];
                entry.mContext = it->second.mContext;
                entry.mType = it->second.mType;
                entry.mResult = static_cast<int32_t>(it->second.mBufferSize);
                entry.mOsError = 0;
                entry.mCompletionTime = 0;

                mPendingOps.erase(it);
                mStats.mPendingRequests--;
                mStats.mTotalCompletions++;
                processedCount++;
            }
        }

        return processedCount;
    }

    // =============================================================================
    // English: Information & Statistics
    // 한글: 정보 및 통계
    // =============================================================================

    const ProviderInfo& EpollAsyncIOProvider::GetInfo() const
    {
        return mInfo;
    }

    ProviderStats EpollAsyncIOProvider::GetStats() const
    {
        return mStats;
    }

    const char* EpollAsyncIOProvider::GetLastError() const
    {
        return mLastError.c_str();
    }

    // =============================================================================
    // English: Factory Function
    // 한글: 팩토리 함수
    // =============================================================================

    std::unique_ptr<AsyncIOProvider> CreateEpollProvider()
    {
        return std::unique_ptr<AsyncIOProvider>(new EpollAsyncIOProvider());
    }

}  // namespace Linux
}  // namespace AsyncIO
}  // namespace Network

#endif  // __linux__

