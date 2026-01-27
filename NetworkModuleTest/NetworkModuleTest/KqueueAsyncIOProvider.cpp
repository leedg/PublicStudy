#ifdef __APPLE__

#include "KqueueAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>

namespace RAON::Network::AsyncIO::BSD
{
    // =============================================================================
    // Constructor & Destructor
    // =============================================================================

    KqueueAsyncIOProvider::KqueueAsyncIOProvider()
        : mKqueueFd(-1)
        , mNextBufferId(0)
        , mMaxConcurrentOps(0)
        , mTotalSendOps(0)
        , mTotalRecvOps(0)
        , mTotalBytesTransferred(0)
        , mInitialized(false)
    {
    }

    KqueueAsyncIOProvider::~KqueueAsyncIOProvider()
    {
        Shutdown();
    }

    // =============================================================================
    // Initialization & Configuration
    // =============================================================================

    bool KqueueAsyncIOProvider::Initialize(uint32_t maxConcurrentOps)
    {
        if (mInitialized)
            return true;

        // Create kqueue file descriptor
        mKqueueFd = kqueue();
        if (mKqueueFd < 0)
            return false;

        mMaxConcurrentOps = maxConcurrentOps;
        mInitialized = true;

        return true;
    }

    void KqueueAsyncIOProvider::Shutdown()
    {
        if (!mInitialized)
            return;

        std::lock_guard<std::mutex> lock(mMutex);

        // Close kqueue file descriptor
        if (mKqueueFd >= 0)
        {
            close(mKqueueFd);
            mKqueueFd = -1;
        }

        mPendingOps.clear();
        mRegisteredSockets.clear();
        mRegisteredBuffers.clear();

        mInitialized = false;
    }

    PlatformInfo KqueueAsyncIOProvider::GetPlatformInfo() const
    {
        return Platform::GetDetailedPlatformInfo();
    }

    bool KqueueAsyncIOProvider::SupportsFeature(const char* featureName) const
    {
        if (std::strcmp(featureName, "SendAsync") == 0) return true;
        if (std::strcmp(featureName, "RecvAsync") == 0) return true;
        if (std::strcmp(featureName, "BufferRegistration") == 0) return false;  // kqueue doesn't support buffer registration
        if (std::strcmp(featureName, "RegisteredI/O") == 0) return false;
        if (std::strcmp(featureName, "EdgeTriggered") == 0) return true;
        return false;
    }

    // =============================================================================
    // Socket Management
    // =============================================================================

    bool KqueueAsyncIOProvider::RegisterSocket(SocketHandle socket)
    {
        if (!mInitialized || socket < 0 || mKqueueFd < 0)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Register socket events with kqueue
        if (!RegisterSocketEvents(socket))
            return false;

        mRegisteredSockets[socket] = true;

        return true;
    }

    bool KqueueAsyncIOProvider::RegisterSocketEvents(SocketHandle socket)
    {
        // Register for both read and write events
        struct kevent events[2];
        
        // Read event
        EV_SET(&events[0], socket, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        
        // Write event
        EV_SET(&events[1], socket, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);

        if (kevent(mKqueueFd, events, 2, nullptr, 0, nullptr) < 0)
            return false;

        return true;
    }

    bool KqueueAsyncIOProvider::UnregisterSocket(SocketHandle socket)
    {
        if (!mInitialized || socket < 0 || mKqueueFd < 0)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Unregister socket events from kqueue
        if (!UnregisterSocketEvents(socket))
            return false;

        // Remove socket from registered list
        auto it = mRegisteredSockets.find(socket);
        if (it != mRegisteredSockets.end())
            mRegisteredSockets.erase(it);

        // Remove pending operations for this socket
        auto op_it = mPendingOps.find(socket);
        if (op_it != mPendingOps.end())
            mPendingOps.erase(op_it);

        return true;
    }

    bool KqueueAsyncIOProvider::UnregisterSocketEvents(SocketHandle socket)
    {
        // Disable and delete read and write events
        struct kevent events[2];
        
        // Delete read event
        EV_SET(&events[0], socket, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        
        // Delete write event
        EV_SET(&events[1], socket, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

        // Ignore errors (socket might be already closed)
        kevent(mKqueueFd, events, 2, nullptr, 0, nullptr);

        return true;
    }

    // =============================================================================
    // Async I/O Operations
    // =============================================================================

    bool KqueueAsyncIOProvider::SendAsync(
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
        pending.socket = socket;
        
        // Allocate and copy buffer
        pending.buffer = std::make_unique<uint8_t[]>(size);
        std::memcpy(pending.buffer.get(), data, size);
        pending.bufferSize = size;

        mPendingOps[socket] = std::move(pending);
        mTotalSendOps++;

        return true;
    }

    bool KqueueAsyncIOProvider::SendAsyncRegistered(
        SocketHandle socket,
        int64_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        // kqueue does not support buffer registration
        return false;
    }

    bool KqueueAsyncIOProvider::RecvAsync(
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
        pending.socket = socket;
        pending.buffer = nullptr;  // Caller manages buffer
        pending.bufferSize = size;

        mPendingOps[socket] = std::move(pending);
        mTotalRecvOps++;

        return true;
    }

    bool KqueueAsyncIOProvider::RecvAsyncRegistered(
        SocketHandle socket,
        int64_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        // kqueue does not support buffer registration
        return false;
    }

    // =============================================================================
    // Buffer Management
    // =============================================================================

    BufferRegistration KqueueAsyncIOProvider::RegisterBuffer(
        const void* buffer,
        uint32_t size,
        BufferPolicy policy
    )
    {
        // kqueue doesn't support pre-registered buffers
        return {-1, false, static_cast<int32_t>(AsyncIOError::PlatformNotSupported)};
    }

    bool KqueueAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
    {
        return false;  // Not supported
    }

    uint32_t KqueueAsyncIOProvider::GetRegisteredBufferCount() const
    {
        return 0;  // Not supported
    }

    // =============================================================================
    // Completion Processing
    // =============================================================================

    uint32_t KqueueAsyncIOProvider::ProcessCompletions(
        CompletionEntry* entries,
        uint32_t maxCount,
        uint32_t timeoutMs
    )
    {
        if (!mInitialized || !entries || maxCount == 0 || mKqueueFd < 0)
            return 0;

        // Prepare timeout structure
        struct timespec ts;
        struct timespec* pts = nullptr;
        
        if (timeoutMs > 0)
        {
            ts.tv_sec = timeoutMs / 1000;
            ts.tv_nsec = (timeoutMs % 1000) * 1000000;
            pts = &ts;
        }

        // Poll for events
        std::unique_ptr<struct kevent[]> events(new struct kevent[maxCount]);
        int numEvents = kevent(mKqueueFd, nullptr, 0, events.get(), maxCount, pts);

        if (numEvents <= 0)
            return 0;

        uint32_t processedCount = 0;

        for (int i = 0; i < numEvents && processedCount < maxCount; ++i)
        {
            struct kevent& event = events[i];
            SocketHandle socket = static_cast<SocketHandle>(event.ident);

            {
                std::lock_guard<std::mutex> lock(mMutex);

                auto it = mPendingOps.find(socket);
                if (it != mPendingOps.end())
                {
                    // Match event type with operation type
                    if ((event.filter == EVFILT_READ && it->second.operationType == AsyncIOType::Recv) ||
                        (event.filter == EVFILT_WRITE && it->second.operationType == AsyncIOType::Send))
                    {
                        CompletionEntry& entry = entries[processedCount];
                        entry.operationType = it->second.operationType;
                        entry.userData = it->second.userData;
                        
                        // Use data field to indicate bytes available
                        entry.bytesTransferred = (event.data > 0) ? event.data : it->second.bufferSize;
                        
                        // Check for errors
                        if (event.flags & EV_ERROR)
                        {
                            entry.errorCode = event.data;
                        }
                        else
                        {
                            entry.errorCode = 0;
                        }
                        entry.internalHandle = socket;

                        // Call user callback
                        if (it->second.callback)
                            it->second.callback(entry, it->second.userData);

                        mTotalBytesTransferred += entry.bytesTransferred;
                        mPendingOps.erase(it);
                        processedCount++;
                    }
                }
            }
        }

        return processedCount;
    }

    // =============================================================================
    // Statistics & Monitoring
    // =============================================================================

    uint32_t KqueueAsyncIOProvider::GetPendingOperationCount() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return static_cast<uint32_t>(mPendingOps.size());
    }

    bool KqueueAsyncIOProvider::GetStatistics(void* outStats) const
    {
        return false;  // Implement if needed
    }

    void KqueueAsyncIOProvider::ResetStatistics()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mTotalSendOps = 0;
        mTotalRecvOps = 0;
        mTotalBytesTransferred = 0;
    }

    // =============================================================================
    // Helper Methods
    // =============================================================================

    bool KqueueAsyncIOProvider::ProcessKqueueEvent(const struct kevent& event)
    {
        // Placeholder for event processing
        return true;
    }

    // =============================================================================
    // Factory Function
    // =============================================================================

    std::unique_ptr<AsyncIOProvider> CreateKqueueProvider()
    {
        return std::make_unique<KqueueAsyncIOProvider>();
    }

}  // namespace RAON::Network::AsyncIO::BSD

#endif  // __APPLE__
