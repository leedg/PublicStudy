#ifdef __linux__

#include "IOUringAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <sys/mman.h>

namespace RAON::Network::AsyncIO::Linux
{
    // =============================================================================
    // Constructor & Destructor
    // =============================================================================

    IOUringAsyncIOProvider::IOUringAsyncIOProvider()
        : mNextBufferId(0)
        , mMaxConcurrentOps(0)
        , mTotalSendOps(0)
        , mTotalRecvOps(0)
        , mTotalBytesTransferred(0)
        , mInitialized(false)
        , mSupportsFixedBuffers(false)
        , mSupportsDirectDescriptors(false)
    {
        std::memset(&mRing, 0, sizeof(io_uring));
    }

    IOUringAsyncIOProvider::~IOUringAsyncIOProvider()
    {
        Shutdown();
    }

    // =============================================================================
    // Initialization & Configuration
    // =============================================================================

    bool IOUringAsyncIOProvider::Initialize(uint32_t maxConcurrentOps)
    {
        if (mInitialized)
            return true;

        mMaxConcurrentOps = maxConcurrentOps;

        // Initialize io_uring ring with specified queue depth
        // Use flags for better performance
        struct io_uring_params params;
        std::memset(&params, 0, sizeof(params));
        
        // Request synchronous completions for simpler implementation
        params.flags = IORING_SETUP_IOPOLL;  // Enable I/O polling

        // Create the ring
        int ret = io_uring_queue_init_params(maxConcurrentOps, &mRing, &params);
        if (ret < 0)
            return false;

        // Check feature support
        if (!CheckFeatureSupport())
        {
            io_uring_queue_exit(&mRing);
            return false;
        }

        mInitialized = true;
        return true;
    }

    void IOUringAsyncIOProvider::Shutdown()
    {
        if (!mInitialized)
            return;

        std::lock_guard<std::mutex> lock(mMutex);

        // Deregister all buffers
        for (auto& pair : mRegisteredBuffers)
        {
            // Note: io_uring doesn't require explicit buffer deregistration
            // Memory will be cleaned up when ring exits
        }
        mRegisteredBuffers.clear();

        // Clear pending operations
        mPendingOps.clear();
        mRegisteredSockets.clear();

        // Exit the ring
        io_uring_queue_exit(&mRing);

        mInitialized = false;
    }

    PlatformInfo IOUringAsyncIOProvider::GetPlatformInfo() const
    {
        return Platform::GetDetailedPlatformInfo();
    }

    bool IOUringAsyncIOProvider::SupportsFeature(const char* featureName) const
    {
        if (std::strcmp(featureName, "SendAsync") == 0) return true;
        if (std::strcmp(featureName, "RecvAsync") == 0) return true;
        if (std::strcmp(featureName, "BufferRegistration") == 0) return mSupportsFixedBuffers;
        if (std::strcmp(featureName, "RegisteredI/O") == 0) return mSupportsFixedBuffers;
        if (std::strcmp(featureName, "DirectDescriptors") == 0) return mSupportsDirectDescriptors;
        if (std::strcmp(featureName, "PollingMode") == 0) return true;
        return false;
    }

    // =============================================================================
    // Feature Support Detection
    // =============================================================================

    bool IOUringAsyncIOProvider::CheckFeatureSupport()
    {
        // Check if io_uring supports fixed buffers (IORING_FEAT_FAST_POLL)
        unsigned int features = mRing.features;

        mSupportsFixedBuffers = (features & IORING_FEAT_FAST_POLL) != 0;
        mSupportsDirectDescriptors = (features & IORING_FEAT_NODROP) != 0;

        return true;  // io_uring itself is supported (or we wouldn't be here)
    }

    // =============================================================================
    // Socket Management
    // =============================================================================

    bool IOUringAsyncIOProvider::RegisterSocket(SocketHandle socket)
    {
        if (!mInitialized || socket < 0)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);
        mRegisteredSockets[socket] = true;

        return true;
    }

    bool IOUringAsyncIOProvider::UnregisterSocket(SocketHandle socket)
    {
        if (!mInitialized || socket < 0)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Remove socket from registered list
        auto it = mRegisteredSockets.find(socket);
        if (it != mRegisteredSockets.end())
            mRegisteredSockets.erase(it);

        // Remove pending operations for this socket
        auto op_it = mPendingOps.begin();
        while (op_it != mPendingOps.end())
        {
            if (op_it->second.socket == socket)
                op_it = mPendingOps.erase(op_it);
            else
                ++op_it;
        }

        return true;
    }

    // =============================================================================
    // Async I/O Operations
    // =============================================================================

    bool IOUringAsyncIOProvider::SendAsync(
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

        // Allocate buffer and copy data
        auto buffer = std::make_unique<uint8_t[]>(size);
        std::memcpy(buffer.get(), data, size);

        // Store pending operation with unique key
        uint64_t opKey = reinterpret_cast<uint64_t>(userData) ^ mTotalSendOps;
        
        PendingOperation pending;
        pending.callback = callback;
        pending.userData = userData;
        pending.operationType = AsyncIOType::Send;
        pending.socket = socket;
        pending.buffer = std::move(buffer);
        pending.bufferSize = size;

        mPendingOps[opKey] = std::move(pending);

        // Prepare send operation in io_uring
        struct io_uring_sqe* sqe = io_uring_get_sqe(&mRing);
        if (!sqe)
            return false;

        io_uring_prep_send(sqe, socket, mPendingOps[opKey].buffer.get(), size, 0);
        sqe->user_data = opKey;

        mTotalSendOps++;

        // Submit to ring
        return SubmitRing();
    }

    bool IOUringAsyncIOProvider::SendAsyncRegistered(
        SocketHandle socket,
        int64_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        if (!mInitialized || socket < 0 || !mSupportsFixedBuffers)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Verify buffer exists
        auto buf_it = mRegisteredBuffers.find(registeredBufferId);
        if (buf_it == mRegisteredBuffers.end())
            return false;

        // Store pending operation
        uint64_t opKey = reinterpret_cast<uint64_t>(userData) ^ (mTotalSendOps + 0x1000);
        
        PendingOperation pending;
        pending.callback = callback;
        pending.userData = userData;
        pending.operationType = AsyncIOType::Send;
        pending.socket = socket;
        pending.buffer = nullptr;
        pending.bufferSize = length;

        mPendingOps[opKey] = std::move(pending);

        // Prepare send operation with registered buffer
        struct io_uring_sqe* sqe = io_uring_get_sqe(&mRing);
        if (!sqe)
            return false;

        // Send from offset in registered buffer
        void* bufAddr = static_cast<uint8_t*>(buf_it->second.address) + offset;
        io_uring_prep_send(sqe, socket, bufAddr, length, 0);
        sqe->user_data = opKey;

        mTotalSendOps++;

        return SubmitRing();
    }

    bool IOUringAsyncIOProvider::RecvAsync(
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
        uint64_t opKey = reinterpret_cast<uint64_t>(userData) ^ (mTotalRecvOps + 0x2000);
        
        PendingOperation pending;
        pending.callback = callback;
        pending.userData = userData;
        pending.operationType = AsyncIOType::Recv;
        pending.socket = socket;
        pending.buffer = nullptr;
        pending.bufferSize = size;

        mPendingOps[opKey] = std::move(pending);

        // Prepare receive operation
        struct io_uring_sqe* sqe = io_uring_get_sqe(&mRing);
        if (!sqe)
            return false;

        io_uring_prep_recv(sqe, socket, buffer, size, 0);
        sqe->user_data = opKey;

        mTotalRecvOps++;

        return SubmitRing();
    }

    bool IOUringAsyncIOProvider::RecvAsyncRegistered(
        SocketHandle socket,
        int64_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        if (!mInitialized || socket < 0 || !mSupportsFixedBuffers)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Verify buffer exists
        auto buf_it = mRegisteredBuffers.find(registeredBufferId);
        if (buf_it == mRegisteredBuffers.end())
            return false;

        // Store pending operation
        uint64_t opKey = reinterpret_cast<uint64_t>(userData) ^ (mTotalRecvOps + 0x3000);
        
        PendingOperation pending;
        pending.callback = callback;
        pending.userData = userData;
        pending.operationType = AsyncIOType::Recv;
        pending.socket = socket;
        pending.buffer = nullptr;
        pending.bufferSize = length;

        mPendingOps[opKey] = std::move(pending);

        // Prepare receive operation with registered buffer
        struct io_uring_sqe* sqe = io_uring_get_sqe(&mRing);
        if (!sqe)
            return false;

        // Receive into offset in registered buffer
        void* bufAddr = static_cast<uint8_t*>(buf_it->second.address) + offset;
        io_uring_prep_recv(sqe, socket, bufAddr, length, 0);
        sqe->user_data = opKey;

        mTotalRecvOps++;

        return SubmitRing();
    }

    // =============================================================================
    // Buffer Management
    // =============================================================================

    BufferRegistration IOUringAsyncIOProvider::RegisterBuffer(
        const void* buffer,
        uint32_t size,
        BufferPolicy policy
    )
    {
        if (!mInitialized || !buffer || size == 0)
            return {-1, false, static_cast<int32_t>(AsyncIOError::InvalidParameter)};

        std::lock_guard<std::mutex> lock(mMutex);

        // Register buffer with io_uring
        // Note: In real implementation, you might use IORING_OP_PROVIDE_BUFFERS
        // For now, we'll use a simple mapping approach
        
        int64_t bufferId = mNextBufferId++;

        RegisteredBuffer regBuf;
        regBuf.address = const_cast<void*>(buffer);
        regBuf.size = size;
        regBuf.bufferGroupId = static_cast<int32_t>(bufferId);
        regBuf.inUse = false;

        mRegisteredBuffers[bufferId] = regBuf;

        return {bufferId, true, 0};
    }

    bool IOUringAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
    {
        if (!mInitialized)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        auto it = mRegisteredBuffers.find(bufferId);
        if (it == mRegisteredBuffers.end())
            return false;

        mRegisteredBuffers.erase(it);
        return true;
    }

    uint32_t IOUringAsyncIOProvider::GetRegisteredBufferCount() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return static_cast<uint32_t>(mRegisteredBuffers.size());
    }

    // =============================================================================
    // Completion Processing
    // =============================================================================

    uint32_t IOUringAsyncIOProvider::ProcessCompletions(
        CompletionEntry* entries,
        uint32_t maxCount,
        uint32_t timeoutMs
    )
    {
        if (!mInitialized || !entries || maxCount == 0)
            return 0;

        std::lock_guard<std::mutex> lock(mMutex);

        // Process available completions
        uint32_t count = ProcessCompletionQueue(entries, maxCount);

        // If no completions available and timeout > 0, wait for completions
        if (count == 0 && timeoutMs > 0)
        {
            // io_uring wait with timeout
            struct __kernel_timespec ts;
            ts.tv_sec = timeoutMs / 1000;
            ts.tv_nsec = (timeoutMs % 1000) * 1000000;

            int ret = io_uring_wait_cqe_timeout(&mRing, nullptr, &ts);
            if (ret == 0)
            {
                count = ProcessCompletionQueue(entries, maxCount);
            }
        }

        return count;
    }

    uint32_t IOUringAsyncIOProvider::ProcessCompletionQueue(
        CompletionEntry* entries,
        uint32_t maxCount
    )
    {
        uint32_t processedCount = 0;
        unsigned head;
        struct io_uring_cqe* cqe;

        io_uring_for_each_cqe(&mRing, head, cqe)
        {
            if (processedCount >= maxCount)
                break;

            uint64_t opKey = cqe->user_data;
            int res = cqe->res;

            auto it = mPendingOps.find(opKey);
            if (it != mPendingOps.end())
            {
                CompletionEntry& entry = entries[processedCount];
                entry.operationType = it->second.operationType;
                entry.userData = it->second.userData;
                entry.bytesTransferred = (res > 0) ? res : 0;
                entry.errorCode = (res < 0) ? -res : 0;
                entry.internalHandle = it->second.socket;

                // Call user callback
                if (it->second.callback)
                    it->second.callback(entry, it->second.userData);

                if (res > 0)
                    mTotalBytesTransferred += res;

                mPendingOps.erase(it);
                processedCount++;
            }
        }

        if (processedCount > 0)
        {
            io_uring_cq_advance(&mRing, processedCount);
        }

        return processedCount;
    }

    // =============================================================================
    // Helper Methods
    // =============================================================================

    bool IOUringAsyncIOProvider::SubmitRing()
    {
        int ret = io_uring_submit(&mRing);
        return ret >= 0;
    }

    // =============================================================================
    // Statistics & Monitoring
    // =============================================================================

    uint32_t IOUringAsyncIOProvider::GetPendingOperationCount() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return static_cast<uint32_t>(mPendingOps.size());
    }

    bool IOUringAsyncIOProvider::GetStatistics(void* outStats) const
    {
        return false;  // Implement if needed
    }

    void IOUringAsyncIOProvider::ResetStatistics()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mTotalSendOps = 0;
        mTotalRecvOps = 0;
        mTotalBytesTransferred = 0;
    }

    // =============================================================================
    // Factory Function
    // =============================================================================

    std::unique_ptr<AsyncIOProvider> CreateIOUringProvider()
    {
        return std::make_unique<IOUringAsyncIOProvider>();
    }

}  // namespace RAON::Network::AsyncIO::Linux

#endif  // __linux__
