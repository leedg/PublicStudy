#ifdef __linux__

#include "EpollAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>

namespace RAON::Network::AsyncIO::Linux
{
    // =============================================================================
    // Constructor & Destructor
    // =============================================================================

    EpollAsyncIOProvider::EpollAsyncIOProvider()
        : mEpollFd(-1)
        , mMaxConcurrentOps(0)
        , mTotalSendOps(0)
        , mTotalRecvOps(0)
        , mTotalBytesTransferred(0)
        , mInitialized(false)
    {
    }

    EpollAsyncIOProvider::~EpollAsyncIOProvider()
    {
        Shutdown();
    }

    // =============================================================================
    // Initialization & Configuration
    // =============================================================================

    bool EpollAsyncIOProvider::Initialize(uint32_t maxConcurrentOps)
    {
        if (mInitialized)
            return true;

        // Create epoll file descriptor
        mEpollFd = epoll_create1(EPOLL_CLOEXEC);
        if (mEpollFd < 0)
            return false;

        mMaxConcurrentOps = maxConcurrentOps;
        mInitialized = true;

        return true;
    }

    void EpollAsyncIOProvider::Shutdown()
    {
        if (!mInitialized)
            return;

        std::lock_guard<std::mutex> lock(mMutex);

        // Close epoll file descriptor
        if (mEpollFd >= 0)
        {
            close(mEpollFd);
            mEpollFd = -1;
        }

        mPendingOps.clear();
        while (!mCompletionQueue.empty())
            mCompletionQueue.pop();

        mInitialized = false;
    }

    PlatformInfo EpollAsyncIOProvider::GetPlatformInfo() const
    {
        return Platform::GetDetailedPlatformInfo();
    }

    bool EpollAsyncIOProvider::SupportsFeature(const char* featureName) const
    {
        if (std::strcmp(featureName, "SendAsync") == 0) return true;
        if (std::strcmp(featureName, "RecvAsync") == 0) return true;
        if (std::strcmp(featureName, "BufferRegistration") == 0) return false;  // epoll doesn't support buffer registration
        if (std::strcmp(featureName, "RegisteredI/O") == 0) return false;
        return false;
    }

    // =============================================================================
    // Socket Management
    // =============================================================================

    bool EpollAsyncIOProvider::RegisterSocket(SocketHandle socket)
    {
        if (!mInitialized || socket < 0 || mEpollFd < 0)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Register socket with epoll
        struct epoll_event event{};
        event.events = EPOLLIN | EPOLLOUT | EPOLLET;  // Edge-triggered
        event.data.fd = socket;

        if (epoll_ctl(mEpollFd, EPOLL_CTL_ADD, socket, &event) < 0)
            return false;

        return true;
    }

    bool EpollAsyncIOProvider::UnregisterSocket(SocketHandle socket)
    {
        if (!mInitialized || socket < 0 || mEpollFd < 0)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Remove socket from epoll
        epoll_ctl(mEpollFd, EPOLL_CTL_DEL, socket, nullptr);

        // Remove pending operations for this socket
        auto it = mPendingOps.find(socket);
        if (it != mPendingOps.end())
            mPendingOps.erase(it);

        return true;
    }

    // =============================================================================
    // Async I/O Operations
    // =============================================================================

    bool EpollAsyncIOProvider::SendAsync(
        SocketHandle socket,
        const void* data,
        uint32_t size,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        if (!mInitialized || socket < 0 || !data || size == 0)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Store pending operation
        PendingOperation pending;
        pending.callback = callback;
        pending.userData = userData;
        pending.operationType = AsyncIOType::Send;
        
        // Allocate and copy buffer
        pending.buffer = std::make_unique<uint8_t[]>(size);
        std::memcpy(pending.buffer.get(), data, size);
        pending.bufferSize = size;

        mPendingOps[socket] = std::move(pending);
        mTotalSendOps++;

        return true;
    }

    bool EpollAsyncIOProvider::SendAsyncRegistered(
        SocketHandle socket,
        int64_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        // epoll does not support buffer registration
        return false;
    }

    bool EpollAsyncIOProvider::RecvAsync(
        SocketHandle socket,
        void* buffer,
        uint32_t size,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        if (!mInitialized || socket < 0 || !buffer || size == 0)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Store pending operation
        PendingOperation pending;
        pending.callback = callback;
        pending.userData = userData;
        pending.operationType = AsyncIOType::Recv;
        
        // Store buffer pointer (caller responsible for buffer lifetime)
        pending.buffer.reset();
        pending.bufferSize = size;

        mPendingOps[socket] = std::move(pending);
        mTotalRecvOps++;

        return true;
    }

    bool EpollAsyncIOProvider::RecvAsyncRegistered(
        SocketHandle socket,
        int64_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        // epoll does not support buffer registration
        return false;
    }

    // =============================================================================
    // Buffer Management
    // =============================================================================

    BufferRegistration EpollAsyncIOProvider::RegisterBuffer(
        const void* buffer,
        uint32_t size,
        BufferPolicy policy
    )
    {
        // epoll doesn't support pre-registered buffers
        return {-1, false, static_cast<int32_t>(AsyncIOError::PlatformNotSupported)};
    }

    bool EpollAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
    {
        return false;  // Not supported
    }

    uint32_t EpollAsyncIOProvider::GetRegisteredBufferCount() const
    {
        return 0;  // Not supported
    }

    // =============================================================================
    // Completion Processing
    // =============================================================================

    uint32_t EpollAsyncIOProvider::ProcessCompletions(
        CompletionEntry* entries,
        uint32_t maxCount,
        uint32_t timeoutMs
    )
    {
        if (!mInitialized || !entries || maxCount == 0 || mEpollFd < 0)
            return 0;

        // Poll for events
        std::unique_ptr<struct epoll_event[]> events(new struct epoll_event[maxCount]);
        int numEvents = epoll_wait(mEpollFd, events.get(), maxCount, timeoutMs);

        if (numEvents < 0)
            return 0;

        uint32_t processedCount = 0;

        for (int i = 0; i < numEvents && processedCount < maxCount; ++i)
        {
            SocketHandle socket = events[i].data.fd;
            
            {
                std::lock_guard<std::mutex> lock(mMutex);

                auto it = mPendingOps.find(socket);
                if (it != mPendingOps.end())
                {
                    CompletionEntry& entry = entries[processedCount];
                    entry.operationType = it->second.operationType;
                    entry.userData = it->second.userData;
                    entry.bytesTransferred = it->second.bufferSize;
                    entry.errorCode = 0;
                    entry.internalHandle = socket;

                    // Call user callback
                    if (it->second.callback)
                        it->second.callback(entry, it->second.userData);

                    mTotalBytesTransferred += it->second.bufferSize;
                    mPendingOps.erase(it);
                    processedCount++;
                }
            }
        }

        return processedCount;
    }

    // =============================================================================
    // Statistics & Monitoring
    // =============================================================================

    uint32_t EpollAsyncIOProvider::GetPendingOperationCount() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return static_cast<uint32_t>(mPendingOps.size());
    }

    bool EpollAsyncIOProvider::GetStatistics(void* outStats) const
    {
        return false;  // Implement if needed
    }

    void EpollAsyncIOProvider::ResetStatistics()
    {
        mTotalSendOps = 0;
        mTotalRecvOps = 0;
        mTotalBytesTransferred = 0;
    }

    // =============================================================================
    // Helper Methods
    // =============================================================================

    bool EpollAsyncIOProvider::ProcessEpollEvent(const struct epoll_event& event)
    {
        // Placeholder for event processing
        return true;
    }

    // =============================================================================
    // Factory Function
    // =============================================================================

    std::unique_ptr<AsyncIOProvider> CreateEpollProvider()
    {
        return std::make_unique<EpollAsyncIOProvider>();
    }

}  // namespace RAON::Network::AsyncIO::Linux

#endif  // __linux__
