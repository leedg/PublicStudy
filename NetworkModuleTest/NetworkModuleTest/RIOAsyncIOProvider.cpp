#ifdef _WIN32

#include "RIOAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstring>
#include <algorithm>

namespace Network::AsyncIO::Windows
{
    // =============================================================================
    // Constructor & Destructor
    // =============================================================================

    RIOAsyncIOProvider::RIOAsyncIOProvider()
        : mCompletionQueue(RIO_INVALID_CQ)
        , mMaxConcurrentOps(0)
        , mTotalSendOps(0)
        , mTotalRecvOps(0)
        , mTotalBytesTransferred(0)
        , mNextBufferId(1)
        , mInitialized(false)
        , pfnRIOCloseCompletionQueue(nullptr)
        , pfnRIOCreateCompletionQueue(nullptr)
        , pfnRIOCreateRequestQueue(nullptr)
        , pfnRIODequeueCompletion(nullptr)
        , pfnRIONotify(nullptr)
        , pfnRIORegisterBuffer(nullptr)
        , pfnRIODeregisterBuffer(nullptr)
        , pfnRIOSend(nullptr)
        , pfnRIORecv(nullptr)
    {
    }

    RIOAsyncIOProvider::~RIOAsyncIOProvider()
    {
        Shutdown();
    }

    // =============================================================================
    // Initialization & Configuration
    // =============================================================================

    bool RIOAsyncIOProvider::Initialize(uint32_t maxConcurrentOps)
    {
        if (mInitialized)
            return true;

        // Load RIO functions
        if (!LoadRIOFunctions())
            return false;

        // Create completion queue
        mCompletionQueue = pfnRIOCreateCompletionQueue(maxConcurrentOps, nullptr);
        if (mCompletionQueue == RIO_INVALID_CQ)
            return false;

        mMaxConcurrentOps = maxConcurrentOps;
        mInitialized = true;

        return true;
    }

    void RIOAsyncIOProvider::Shutdown()
    {
        if (!mInitialized)
            return;

        std::lock_guard<std::mutex> lock(mMutex);

        // Close all request queues
        for (auto& pair : mRequestQueues)
        {
            if (pair.second != RIO_INVALID_RQ)
            {
                // Close request queue (implementation specific)
            }
        }
        mRequestQueues.clear();

        // Deregister all buffers
        for (auto& pair : mRegisteredBuffers)
        {
            if (pfnRIODeregisterBuffer)
                pfnRIODeregisterBuffer(pair.second.rioBufferId);
        }
        mRegisteredBuffers.clear();

        // Close completion queue
        if (mCompletionQueue != RIO_INVALID_CQ && pfnRIOCloseCompletionQueue)
        {
            pfnRIOCloseCompletionQueue(mCompletionQueue);
            mCompletionQueue = RIO_INVALID_CQ;
        }

        mInitialized = false;
    }

    PlatformInfo RIOAsyncIOProvider::GetPlatformInfo() const
    {
        return Platform::GetDetailedPlatformInfo();
    }

    bool RIOAsyncIOProvider::SupportsFeature(const char* featureName) const
    {
        if (std::strcmp(featureName, "SendAsync") == 0) return true;
        if (std::strcmp(featureName, "RecvAsync") == 0) return true;
        if (std::strcmp(featureName, "SendAsyncRegistered") == 0) return true;
        if (std::strcmp(featureName, "RecvAsyncRegistered") == 0) return true;
        if (std::strcmp(featureName, "BufferRegistration") == 0) return true;
        if (std::strcmp(featureName, "RegisteredI/O") == 0) return true;
        return false;
    }

    // =============================================================================
    // Socket Management
    // =============================================================================

    bool RIOAsyncIOProvider::RegisterSocket(SocketHandle socket)
    {
        if (!mInitialized || socket == INVALID_SOCKET || !pfnRIOCreateRequestQueue)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Create request queue for this socket
        RIO_RQ requestQueue = pfnRIOCreateRequestQueue(socket, mMaxConcurrentOps, mMaxConcurrentOps, mCompletionQueue);
        if (requestQueue == RIO_INVALID_RQ)
            return false;

        mRequestQueues[socket] = requestQueue;
        return true;
    }

    bool RIOAsyncIOProvider::UnregisterSocket(SocketHandle socket)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        auto it = mRequestQueues.find(socket);
        if (it != mRequestQueues.end())
        {
            mRequestQueues.erase(it);
        }

        // Also remove pending operations for this socket
        auto opIt = mPendingOps.begin();
        while (opIt != mPendingOps.end())
        {
            if (opIt->second.socket == socket)
                opIt = mPendingOps.erase(opIt);
            else
                ++opIt;
        }

        return true;
    }

    // =============================================================================
    // Async I/O Operations
    // =============================================================================

    bool RIOAsyncIOProvider::SendAsync(
        SocketHandle socket,
        const void* data,
        uint32_t size,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        if (!mInitialized || socket == INVALID_SOCKET || !data || size == 0 || !pfnRIOSend)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Find request queue for this socket
        auto it = mRequestQueues.find(socket);
        if (it == mRequestQueues.end() || it->second == RIO_INVALID_RQ)
            return false;

        // For non-registered buffers, we need to use IOCP fallback
        // RIO requires pre-registered buffers for optimal performance
        return false;
    }

    bool RIOAsyncIOProvider::SendAsyncRegistered(
        SocketHandle socket,
        int64_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        if (!mInitialized || socket == INVALID_SOCKET || !pfnRIOSend)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Find request queue for this socket
        auto queueIt = mRequestQueues.find(socket);
        if (queueIt == mRequestQueues.end() || queueIt->second == RIO_INVALID_RQ)
            return false;

        // Find registered buffer
        auto bufIt = mRegisteredBuffers.find(registeredBufferId);
        if (bufIt == mRegisteredBuffers.end())
            return false;

        // Create RIO_BUF structure
        RIO_BUF rioBuf;
        rioBuf.BufferId = bufIt->second.rioBufferId;
        rioBuf.Offset = offset;
        rioBuf.Length = length;

        // Submit send request
        int result = pfnRIOSend(queueIt->second, &rioBuf, 1, flags, userData);
        if (result == SOCKET_ERROR)
            return false;

        // Store pending operation
        PendingOperation pending;
        pending.callback = callback;
        pending.userData = userData;
        pending.socket = socket;
        pending.operationType = AsyncIOType::Send;

        mPendingOps[userData] = pending;
        mTotalSendOps++;

        return true;
    }

    bool RIOAsyncIOProvider::RecvAsync(
        SocketHandle socket,
        void* buffer,
        uint32_t size,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        // Non-registered recv not recommended for RIO
        return false;
    }

    bool RIOAsyncIOProvider::RecvAsyncRegistered(
        SocketHandle socket,
        int64_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        if (!mInitialized || socket == INVALID_SOCKET || !pfnRIORecv)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Find request queue for this socket
        auto queueIt = mRequestQueues.find(socket);
        if (queueIt == mRequestQueues.end() || queueIt->second == RIO_INVALID_RQ)
            return false;

        // Find registered buffer
        auto bufIt = mRegisteredBuffers.find(registeredBufferId);
        if (bufIt == mRegisteredBuffers.end())
            return false;

        // Create RIO_BUF structure
        RIO_BUF rioBuf;
        rioBuf.BufferId = bufIt->second.rioBufferId;
        rioBuf.Offset = offset;
        rioBuf.Length = length;

        // Submit receive request
        int result = pfnRIORecv(queueIt->second, &rioBuf, 1, flags, userData);
        if (result == SOCKET_ERROR)
            return false;

        // Store pending operation
        PendingOperation pending;
        pending.callback = callback;
        pending.userData = userData;
        pending.socket = socket;
        pending.operationType = AsyncIOType::Recv;

        mPendingOps[userData] = pending;
        mTotalRecvOps++;

        return true;
    }

    // =============================================================================
    // Buffer Management
    // =============================================================================

    BufferRegistration RIOAsyncIOProvider::RegisterBuffer(
        const void* buffer,
        uint32_t size,
        BufferPolicy policy
    )
    {
        if (!mInitialized || !buffer || size == 0 || !pfnRIORegisterBuffer)
        {
            return {-1, false, static_cast<int32_t>(AsyncIOError::InvalidParameter)};
        }

        std::lock_guard<std::mutex> lock(mMutex);

        // Register buffer with RIO
        RIO_BUFFERID rioBufferId = pfnRIORegisterBuffer(const_cast<PCHAR>(static_cast<const char*>(buffer)), size);
        if (rioBufferId == RIO_INVALID_BUFFERID)
        {
            return {-1, false, static_cast<int32_t>(AsyncIOError::AllocationFailed)};
        }

        // Store registration
        int64_t bufferId = mNextBufferId++;
        mRegisteredBuffers[bufferId] = {rioBufferId, const_cast<void*>(buffer), size, policy};

        return {bufferId, true, 0};
    }

    bool RIOAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
    {
        if (!mInitialized || !pfnRIODeregisterBuffer)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        auto it = mRegisteredBuffers.find(bufferId);
        if (it == mRegisteredBuffers.end())
            return false;

        // Deregister from RIO
        int result = pfnRIODeregisterBuffer(it->second.rioBufferId);
        if (result != 0)
            return false;

        mRegisteredBuffers.erase(it);
        return true;
    }

    uint32_t RIOAsyncIOProvider::GetRegisteredBufferCount() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return static_cast<uint32_t>(mRegisteredBuffers.size());
    }

    // =============================================================================
    // Completion Processing
    // =============================================================================

    uint32_t RIOAsyncIOProvider::ProcessCompletions(
        CompletionEntry* entries,
        uint32_t maxCount,
        uint32_t timeoutMs
    )
    {
        if (!mInitialized || !entries || maxCount == 0 || !pfnRIODequeueCompletion)
            return 0;

        std::lock_guard<std::mutex> lock(mMutex);

        // Allocate temporary buffer for RIO results
        std::unique_ptr<RIORESULT[]> rioResults(new RIORESULT[maxCount]);

        // Dequeue completions from RIO
        ULONG completionCount = pfnRIODequeueCompletion(mCompletionQueue, rioResults.get(), maxCount);
        if (completionCount == RIO_CORRUPT_CQ)
        {
            return 0;
        }

        // Convert RIO results to CompletionEntry
        for (ULONG i = 0; i < completionCount; ++i)
        {
            ConvertRIOResult(rioResults[i], entries[i]);

            // Call user callback
            void* requestContext = reinterpret_cast<void*>(rioResults[i].RequestContext);
            auto opIt = mPendingOps.find(requestContext);
            if (opIt != mPendingOps.end() && opIt->second.callback)
            {
                opIt->second.callback(entries[i], opIt->second.userData);
                mPendingOps.erase(opIt);
            }

            mTotalBytesTransferred += rioResults[i].BytesTransferred;
        }

        return completionCount;
    }

    // =============================================================================
    // Statistics & Monitoring
    // =============================================================================

    uint32_t RIOAsyncIOProvider::GetPendingOperationCount() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return static_cast<uint32_t>(mPendingOps.size());
    }

    bool RIOAsyncIOProvider::GetStatistics(void* outStats) const
    {
        return false;  // Implement if needed
    }

    void RIOAsyncIOProvider::ResetStatistics()
    {
        mTotalSendOps = 0;
        mTotalRecvOps = 0;
        mTotalBytesTransferred = 0;
    }

    // =============================================================================
    // Helper Methods
    // =============================================================================

    bool RIOAsyncIOProvider::LoadRIOFunctions()
    {
        HMODULE hMswsock = LoadLibraryA("mswsock.dll");
        if (!hMswsock)
            return false;

        // Load RIO function pointers
        pfnRIOCloseCompletionQueue = (PfnRIOCloseCompletionQueue)GetProcAddress(hMswsock, "RIOCloseCompletionQueue");
        pfnRIOCreateCompletionQueue = (PfnRIOCreateCompletionQueue)GetProcAddress(hMswsock, "RIOCreateCompletionQueue");
        pfnRIOCreateRequestQueue = (PfnRIOCreateRequestQueue)GetProcAddress(hMswsock, "RIOCreateRequestQueue");
        pfnRIODequeueCompletion = (PfnRIODequeueCompletion)GetProcAddress(hMswsock, "RIODequeueCompletion");
        pfnRIONotify = (PfnRIONotify)GetProcAddress(hMswsock, "RIONotify");
        pfnRIORegisterBuffer = (PfnRIORegisterBuffer)GetProcAddress(hMswsock, "RIORegisterBuffer");
        pfnRIODeregisterBuffer = (PfnRIODeregisterBuffer)GetProcAddress(hMswsock, "RIODeregisterBuffer");
        pfnRIOSend = (PfnRIOSend)GetProcAddress(hMswsock, "RIOSend");
        pfnRIORecv = (PfnRIORecv)GetProcAddress(hMswsock, "RIORecv");

        // Check if all functions loaded
        return (pfnRIOCloseCompletionQueue && pfnRIOCreateCompletionQueue && 
                pfnRIOCreateRequestQueue && pfnRIODequeueCompletion && 
                pfnRIORegisterBuffer && pfnRIODeregisterBuffer && 
                pfnRIOSend && pfnRIORecv);
    }

    bool RIOAsyncIOProvider::ConvertRIOResult(
        const RIORESULT& rioResult,
        CompletionEntry& outEntry
    )
    {
        outEntry.bytesTransferred = rioResult.BytesTransferred;
        outEntry.errorCode = rioResult.Status;
        outEntry.internalHandle = reinterpret_cast<uint64_t>(&rioResult);
        // operationType and userData should be retrieved from pending ops
        return true;
    }

    // =============================================================================
    // Factory Function
    // =============================================================================

    std::unique_ptr<AsyncIOProvider> CreateRIOProvider()
    {
        return std::make_unique<RIOAsyncIOProvider>();
    }

}  // namespace Network::AsyncIO::Windows

#endif  // _WIN32
