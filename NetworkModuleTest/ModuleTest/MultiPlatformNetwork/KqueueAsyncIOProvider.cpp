// English: kqueue-based AsyncIOProvider implementation for macOS/BSD
// ?쒓?: macOS/BSD??kqueue 湲곕컲 AsyncIOProvider 援ы쁽

#ifdef __APPLE__

#include "KqueueAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <unistd.h>
#include <cstring>

namespace Network {
namespace AsyncIO {
namespace BSD {
    // =============================================================================
    // English: Constructor & Destructor
    // ?쒓?: ?앹꽦??諛??뚮㈇??    // =============================================================================

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
    // ?쒓?: ?앸챸二쇨린 愿由?
    // =============================================================================

    AsyncIOError KqueueAsyncIOProvider::Initialize(size_t queueDepth, size_t maxConcurrent)
    {
        if (mInitialized)
            return AsyncIOError::AlreadyInitialized;

        // English: Create kqueue file descriptor
        // ?쒓?: kqueue ?뚯씪 ?붿뒪?щ┰???앹꽦
        mKqueueFd = kqueue();
        if (mKqueueFd < 0)
        {
            mLastError = "kqueue() failed";
            return AsyncIOError::OperationFailed;
        }

        mMaxConcurrentOps = maxConcurrent;

        // English: Initialize provider info
        // ?쒓?: 怨듦툒???뺣낫 珥덇린??
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
        // ?쒓?: kqueue ?뚯씪 ?붿뒪?щ┰???リ린
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
    // ?쒓?: 踰꾪띁 愿由?
    // =============================================================================

    int64_t KqueueAsyncIOProvider::RegisterBuffer(const void* ptr, size_t size)
    {
        // English: kqueue doesn't support pre-registered buffers (no-op)
        // ?쒓?: kqueue???ъ쟾 ?깅줉 踰꾪띁瑜?吏?먰븯吏 ?딆쓬 (no-op)
        return -1;
    }

    AsyncIOError KqueueAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
    {
        return AsyncIOError::PlatformNotSupported;
    }

    // =============================================================================
    // English: Async I/O Operations
    // ?쒓?: 鍮꾨룞湲?I/O ?묒뾽
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
        // ?쒓?: 踰꾪띁 蹂듭궗? ?④퍡 ?湲??묒뾽 ???
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
        // ?쒓?: kqueue??諛곗튂 泥섎━瑜?吏?먰븯吏 ?딆쓬 (no-op)
        if (!mInitialized)
            return AsyncIOError::NotInitialized;

        return AsyncIOError::Success;
    }

    // =============================================================================
    // English: Completion Processing
    // ?쒓?: ?꾨즺 泥섎━
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
        // ?쒓?: ??꾩븘??援ъ“泥?以鍮?
        struct timespec ts;
        struct timespec* pts = nullptr;

        if (timeoutMs >= 0)
        {
            ts.tv_sec = timeoutMs / 1000;
            ts.tv_nsec = (timeoutMs % 1000) * 1000000;
            pts = &ts;
        }

        // English: Poll for events
        // ?쒓?: ?대깽???대쭅
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
                // ?쒓?: ?대깽????낃낵 ?묒뾽 ???留ㅼ묶
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
    // ?쒓?: ?ы띁 硫붿꽌??
    // =============================================================================

    bool KqueueAsyncIOProvider::RegisterSocketEvents(SocketHandle socket)
    {
        // English: Register for both read and write events
        // ?쒓?: ?쎄린 諛??곌린 ?대깽???깅줉
        struct kevent events[2];
        EV_SET(&events[0], socket, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        EV_SET(&events[1], socket, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);

        return kevent(mKqueueFd, events, 2, nullptr, 0, nullptr) >= 0;
    }

    bool KqueueAsyncIOProvider::UnregisterSocketEvents(SocketHandle socket)
    {
        // English: Delete read and write events
        // ?쒓?: ?쎄린 諛??곌린 ?대깽????젣
        struct kevent events[2];
        EV_SET(&events[0], socket, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        EV_SET(&events[1], socket, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

        // English: Ignore errors (socket might already be closed)
        // ?쒓?: ?먮윭 臾댁떆 (?뚯폆???대? ?ロ삍?????덉쓬)
        kevent(mKqueueFd, events, 2, nullptr, 0, nullptr);
        return true;
    }

    // =============================================================================
    // English: Information & Statistics
    // ?쒓?: ?뺣낫 諛??듦퀎
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
    // ?쒓?: ?⑺넗由??⑥닔
    // =============================================================================

    std::unique_ptr<AsyncIOProvider> CreateKqueueProvider()
    {
        return std::unique_ptr<AsyncIOProvider>(new KqueueAsyncIOProvider());
    }

}  // namespace BSD
}  // namespace AsyncIO
}  // namespace Network

#endif  // __APPLE__

