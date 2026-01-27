#ifdef _WIN32

#include "IocpAsyncIOProvider.h"
#include "PlatformDetect.h"
#include <cstring>

namespace Network::AsyncIO::Windows
{
    // =============================================================================
    // Constructor & Destructor
    // =============================================================================

    IocpAsyncIOProvider::IocpAsyncIOProvider()
        : mCompletionPort(INVALID_HANDLE_VALUE)
        , mMaxConcurrentOps(0)
        , mTotalSendOps(0)
        , mTotalRecvOps(0)
        , mTotalBytesTransferred(0)
        , mInitialized(false)
    {
    }

    IocpAsyncIOProvider::~IocpAsyncIOProvider()
    {
        Shutdown();
    }

    // =============================================================================
    // Initialization & Configuration
    // =============================================================================

    bool IocpAsyncIOProvider::Initialize(uint32_t maxConcurrentOps)
    {
        if (mInitialized)
            return true;

        // Create IOCP handle
        mCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (mCompletionPort == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        mMaxConcurrentOps = maxConcurrentOps;
        mInitialized = true;

        return true;
    }

    void IocpAsyncIOProvider::Shutdown()
    {
        if (!mInitialized)
            return;

        // Close all pending operations
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mPendingOps.clear();
        }

        // Close IOCP handle
        if (mCompletionPort != INVALID_HANDLE_VALUE)
        {
            CloseHandle(mCompletionPort);
            mCompletionPort = INVALID_HANDLE_VALUE;
        }

        mInitialized = false;
    }

    PlatformInfo IocpAsyncIOProvider::GetPlatformInfo() const
    {
        return Platform::GetDetailedPlatformInfo();
    }

    bool IocpAsyncIOProvider::SupportsFeature(const char* featureName) const
    {
        if (std::strcmp(featureName, "SendAsync") == 0) return true;
        if (std::strcmp(featureName, "RecvAsync") == 0) return true;
        if (std::strcmp(featureName, "BufferRegistration") == 0) return false;  // IOCP doesn't support buffer registration
        if (std::strcmp(featureName, "RegisteredI/O") == 0) return false;
        return false;
    }

    // =============================================================================
    // Socket Management
    // =============================================================================

    bool IocpAsyncIOProvider::RegisterSocket(SocketHandle socket)
    {
        if (!mInitialized || socket == INVALID_SOCKET)
            return false;

        // Associate socket with IOCP
        HANDLE result = CreateIoCompletionPort(
            reinterpret_cast<HANDLE>(socket),
            mCompletionPort,
            reinterpret_cast<ULONG_PTR>(socket),
            0
        );

        return result == mCompletionPort;
    }

    bool IocpAsyncIOProvider::UnregisterSocket(SocketHandle socket)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        
        auto it = mPendingOps.find(socket);
        if (it != mPendingOps.end())
        {
            mPendingOps.erase(it);
        }

        return true;
    }

    // =============================================================================
    // Async I/O Operations
    // =============================================================================

    bool IocpAsyncIOProvider::SendAsync(
        SocketHandle socket,
        const void* data,
        uint32_t size,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        if (!mInitialized || socket == INVALID_SOCKET || !data || size == 0)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Create pending operation
        auto pendingOp = std::make_unique<PendingOperation>();
        pendingOp->callback = callback;
        pendingOp->userData = userData;
        
        // Copy data to internal buffer
        pendingOp->buffer = std::make_unique<uint8_t[]>(size);
        std::memcpy(pendingOp->buffer.get(), data, size);

        // Setup WSABUF
        pendingOp->wsaBuffer.buf = reinterpret_cast<char*>(pendingOp->buffer.get());
        pendingOp->wsaBuffer.len = size;

        // Initialize OVERLAPPED
        std::memset(&pendingOp->overlapped, 0, sizeof(OVERLAPPED));

        // Issue WSASend
        DWORD bytesSent = 0;
        int result = WSASend(
            socket,
            &pendingOp->wsaBuffer,
            1,
            &bytesSent,
            flags,
            &pendingOp->overlapped,
            nullptr
        );

        if (result == SOCKET_ERROR)
        {
            DWORD error = WSAGetLastError();
            if (error != WSA_IO_PENDING)
            {
                return false;
            }
        }

        // Store pending operation
        mPendingOps[socket] = std::move(pendingOp);
        mTotalSendOps++;

        return true;
    }

    bool IocpAsyncIOProvider::SendAsyncRegistered(
        SocketHandle socket,
        int64_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        // IOCP does not support buffer registration
        // Fall back to regular SendAsync if needed
        return false;
    }

    bool IocpAsyncIOProvider::RecvAsync(
        SocketHandle socket,
        void* buffer,
        uint32_t size,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        if (!mInitialized || socket == INVALID_SOCKET || !buffer || size == 0)
            return false;

        std::lock_guard<std::mutex> lock(mMutex);

        // Create pending operation
        auto pendingOp = std::make_unique<PendingOperation>();
        pendingOp->callback = callback;
        pendingOp->userData = userData;
        
        // Setup WSABUF (use provided buffer directly for recv)
        pendingOp->wsaBuffer.buf = static_cast<char*>(buffer);
        pendingOp->wsaBuffer.len = size;

        // Initialize OVERLAPPED
        std::memset(&pendingOp->overlapped, 0, sizeof(OVERLAPPED));

        // Issue WSARecv
        DWORD bytesRecvd = 0;
        int result = WSARecv(
            socket,
            &pendingOp->wsaBuffer,
            1,
            &bytesRecvd,
            &flags,
            &pendingOp->overlapped,
            nullptr
        );

        if (result == SOCKET_ERROR)
        {
            DWORD error = WSAGetLastError();
            if (error != WSA_IO_PENDING)
            {
                return false;
            }
        }

        // Store pending operation
        mPendingOps[socket] = std::move(pendingOp);
        mTotalRecvOps++;

        return true;
    }

    bool IocpAsyncIOProvider::RecvAsyncRegistered(
        SocketHandle socket,
        int64_t registeredBufferId,
        uint32_t offset,
        uint32_t length,
        void* userData,
        uint32_t flags,
        CompletionCallback callback
    )
    {
        // IOCP does not support buffer registration
        return false;
    }

    // =============================================================================
    // Buffer Management
    // =============================================================================

    BufferRegistration IocpAsyncIOProvider::RegisterBuffer(
        const void* buffer,
        uint32_t size,
        BufferPolicy policy
    )
    {
        // IOCP doesn't support pre-registered buffers
        return {-1, false, static_cast<int32_t>(AsyncIOError::PlatformNotSupported)};
    }

    bool IocpAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
    {
        return false;  // Not supported
    }

    uint32_t IocpAsyncIOProvider::GetRegisteredBufferCount() const
    {
        return 0;  // Not supported
    }

    // =============================================================================
    // Completion Processing
    // =============================================================================

    uint32_t IocpAsyncIOProvider::ProcessCompletions(
        CompletionEntry* entries,
        uint32_t maxCount,
        uint32_t timeoutMs
    )
    {
        if (!mInitialized || !entries || maxCount == 0)
            return 0;

        uint32_t processedCount = 0;

        for (uint32_t i = 0; i < maxCount; ++i)
        {
            DWORD bytesTransferred = 0;
            ULONG_PTR completionKey = 0;
            LPOVERLAPPED pOverlapped = nullptr;

            BOOL success = GetQueuedCompletionStatus(
                mCompletionPort,
                &bytesTransferred,
                &completionKey,
                &pOverlapped,
                (i == 0) ? timeoutMs : 0  // Use timeout only on first iteration
            );

            if (!success && pOverlapped == nullptr)
            {
                // No more completions
                break;
            }

            // Process this completion
            if (pOverlapped != nullptr)
            {
                CompletionEntry& entry = entries[i];
                ConvertIOCPResult(pOverlapped, bytesTransferred, entry);
                
                processedCount++;
                mTotalBytesTransferred += bytesTransferred;

                // Call user callback
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    auto it = mPendingOps.find(static_cast<SocketHandle>(completionKey));
                    if (it != mPendingOps.end() && it->second->callback)
                    {
                        it->second->callback(entry, it->second->userData);
                        mPendingOps.erase(it);
                    }
                }
            }
        }

        return processedCount;
    }

    // =============================================================================
    // Statistics & Monitoring
    // =============================================================================

    uint32_t IocpAsyncIOProvider::GetPendingOperationCount() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return static_cast<uint32_t>(mPendingOps.size());
    }

    bool IocpAsyncIOProvider::GetStatistics(void* outStats) const
    {
        // Implement if needed by application
        return false;
    }

    void IocpAsyncIOProvider::ResetStatistics()
    {
        mTotalSendOps = 0;
        mTotalRecvOps = 0;
        mTotalBytesTransferred = 0;
    }

    // =============================================================================
    // Helper Methods
    // =============================================================================

    bool IocpAsyncIOProvider::ConvertIOCPResult(
        OVERLAPPED* overlapped,
        DWORD bytesTransferred,
        CompletionEntry& outEntry
    )
    {
        outEntry.bytesTransferred = bytesTransferred;
        outEntry.internalHandle = reinterpret_cast<uint64_t>(overlapped);
        outEntry.errorCode = 0;  // Assume success
        // operationType and userData should be set by caller
        return true;
    }

    WSABUF IocpAsyncIOProvider::CreateWSABuffer(const void* data, uint32_t size)
    {
        WSABUF buf;
        buf.buf = const_cast<char*>(static_cast<const char*>(data));
        buf.len = size;
        return buf;
    }

    // =============================================================================
    // Factory Function
    // =============================================================================

    std::unique_ptr<AsyncIOProvider> CreateIocpProvider()
    {
        return std::make_unique<IocpAsyncIOProvider>();
    }

}  // namespace Network::AsyncIO::Windows

#endif  // _WIN32
