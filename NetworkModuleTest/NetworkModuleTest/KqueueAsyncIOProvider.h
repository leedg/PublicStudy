#pragma once

#include "AsyncIOProvider.h"

#ifdef __APPLE__
#include <sys/event.h>
#include <map>
#include <memory>
#include <mutex>
#include <queue>

namespace RAON::Network::AsyncIO::BSD
{
    // =============================================================================
    // kqueue-based AsyncIOProvider Implementation (macOS/BSD)
    // =============================================================================

    class KqueueAsyncIOProvider : public AsyncIOProvider
    {
    public:
        KqueueAsyncIOProvider();
        virtual ~KqueueAsyncIOProvider();

        // Prevent copy
        KqueueAsyncIOProvider(const KqueueAsyncIOProvider&) = delete;
        KqueueAsyncIOProvider& operator=(const KqueueAsyncIOProvider&) = delete;

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
            CompletionCallback callback;
            void* userData;
            AsyncIOType operationType;
            SocketHandle socket;
            std::unique_ptr<uint8_t[]> buffer;  // For dynamically allocated buffers
            uint32_t bufferSize;
        };

        struct RegisteredBuffer
        {
            void* address;
            uint32_t size;
            bool inUse;
        };

        // =====================================================================
        // Member Variables
        // =====================================================================

        int mKqueueFd;
        std::map<SocketHandle, PendingOperation> mPendingOps;
        std::map<int64_t, RegisteredBuffer> mRegisteredBuffers;
        std::map<SocketHandle, bool> mRegisteredSockets;
        
        std::mutex mMutex;
        
        uint32_t mMaxConcurrentOps;
        int64_t mNextBufferId;
        uint64_t mTotalSendOps;
        uint64_t mTotalRecvOps;
        uint64_t mTotalBytesTransferred;
        
        bool mInitialized;

        // =====================================================================
        // Helper Methods
        // =====================================================================

        /**
         * Register socket with kqueue for read and write events
         */
        bool RegisterSocketEvents(SocketHandle socket);

        /**
         * Unregister socket events from kqueue
         */
        bool UnregisterSocketEvents(SocketHandle socket);

        /**
         * Process kevent and queue completions
         */
        bool ProcessKqueueEvent(const struct kevent& event);
    };

}  // namespace RAON::Network::AsyncIO::BSD

#endif  // __APPLE__
