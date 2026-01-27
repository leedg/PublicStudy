#pragma once

#include "AsyncIOProvider.h"

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <map>
#include <memory>
#include <mutex>

namespace Network::AsyncIO::Windows
{
    // =============================================================================
    // IOCP-based AsyncIOProvider Implementation
    // =============================================================================

    class IocpAsyncIOProvider : public AsyncIOProvider
    {
    public:
        IocpAsyncIOProvider();
        virtual ~IocpAsyncIOProvider();

        // Prevent copy
        IocpAsyncIOProvider(const IocpAsyncIOProvider&) = delete;
        IocpAsyncIOProvider& operator=(const IocpAsyncIOProvider&) = delete;

        // =====================================================================
        // Initialization & Configuration
        // =====================================================================

        bool Initialize(uint32_t maxConcurrentOps = 10000) override;
        void Shutdown() override;
        
        PlatformInfo GetPlatformInfo() const override;
        bool SupportsFeature(const char* featureName) const override;

        // =====================================================================
        // Socket Management
        // =====================================================================

        bool RegisterSocket(SocketHandle socket) override;
        bool UnregisterSocket(SocketHandle socket) override;

        // =====================================================================
        // Async I/O Operations
        // =====================================================================

        bool SendAsync(
            SocketHandle socket,
            const void* data,
            uint32_t size,
            void* userData,
            uint32_t flags,
            CompletionCallback callback
        ) override;

        bool SendAsyncRegistered(
            SocketHandle socket,
            int64_t registeredBufferId,
            uint32_t offset,
            uint32_t length,
            void* userData,
            uint32_t flags,
            CompletionCallback callback
        ) override;

        bool RecvAsync(
            SocketHandle socket,
            void* buffer,
            uint32_t size,
            void* userData,
            uint32_t flags,
            CompletionCallback callback
        ) override;

        bool RecvAsyncRegistered(
            SocketHandle socket,
            int64_t registeredBufferId,
            uint32_t offset,
            uint32_t length,
            void* userData,
            uint32_t flags,
            CompletionCallback callback
        ) override;

        // =====================================================================
        // Buffer Management
        // =====================================================================

        BufferRegistration RegisterBuffer(
            const void* buffer,
            uint32_t size,
            BufferPolicy policy = BufferPolicy::Reuse
        ) override;

        bool UnregisterBuffer(int64_t bufferId) override;
        uint32_t GetRegisteredBufferCount() const override;

        // =====================================================================
        // Completion Processing
        // =====================================================================

        uint32_t ProcessCompletions(
            CompletionEntry* entries,
            uint32_t maxCount,
            uint32_t timeoutMs
        ) override;

        // =====================================================================
        // Statistics & Monitoring
        // =====================================================================

        uint32_t GetPendingOperationCount() const override;
        bool GetStatistics(void* outStats) const override;
        void ResetStatistics() override;

    private:
        // =====================================================================
        // Internal Data Structures
        // =====================================================================

        struct PendingOperation
        {
            OVERLAPPED overlapped;
            CompletionCallback callback;
            void* userData;
            WSABUF wsaBuffer;
            std::unique_ptr<uint8_t[]> buffer;  // For dynamically allocated buffers
        };

        // =====================================================================
        // Member Variables
        // =====================================================================

        HANDLE mCompletionPort;
        std::map<SocketHandle, std::unique_ptr<PendingOperation>> mPendingOps;
        mutable std::mutex mMutex;
        
        uint32_t mMaxConcurrentOps;
        uint64_t mTotalSendOps;
        uint64_t mTotalRecvOps;
        uint64_t mTotalBytesTransferred;
        
        bool mInitialized;

        // =====================================================================
        // Helper Methods
        // =====================================================================

        /**
         * Convert IOCP completion key to CompletionEntry
         */
        bool ConvertIOCPResult(
            OVERLAPPED* overlapped,
            DWORD bytesTransferred,
            CompletionEntry& outEntry
        );

        /**
         * Create a WSABUF structure for sending/receiving
         */
        WSABUF CreateWSABuffer(const void* data, uint32_t size);
    };

}  // namespace Network::AsyncIO::Windows

#endif  // _WIN32
