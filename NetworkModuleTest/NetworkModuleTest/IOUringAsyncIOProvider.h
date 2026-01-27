#pragma once

#include "AsyncIOProvider.h"

#ifdef __linux__
#include <liburing.h>
#include <map>
#include <memory>
#include <mutex>
#include <queue>

namespace RAON::Network::AsyncIO::Linux
{
    // =============================================================================
    // io_uring-based AsyncIOProvider Implementation (Linux kernel 5.1+)
    // =============================================================================

    class IOUringAsyncIOProvider : public AsyncIOProvider
    {
    public:
        IOUringAsyncIOProvider();
        virtual ~IOUringAsyncIOProvider();

        // Prevent copy
        IOUringAsyncIOProvider(const IOUringAsyncIOProvider&) = delete;
        IOUringAsyncIOProvider& operator=(const IOUringAsyncIOProvider&) = delete;

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
            int32_t bufferGroupId;
            bool inUse;
        };

        // =====================================================================
        // Member Variables
        // =====================================================================

        io_uring mRing;
        std::map<uint64_t, PendingOperation> mPendingOps;  // Map by user_data
        std::map<int64_t, RegisteredBuffer> mRegisteredBuffers;
        std::map<SocketHandle, bool> mRegisteredSockets;
        
        std::mutex mMutex;
        
        uint32_t mMaxConcurrentOps;
        int64_t mNextBufferId;
        uint64_t mTotalSendOps;
        uint64_t mTotalRecvOps;
        uint64_t mTotalBytesTransferred;
        
        bool mInitialized;
        bool mSupportsFixedBuffers;
        bool mSupportsDirectDescriptors;

        // =====================================================================
        // Helper Methods
        // =====================================================================

        /**
         * Check io_uring feature support
         */
        bool CheckFeatureSupport();

        /**
         * Submit pending operations to the ring
         */
        bool SubmitRing();

        /**
         * Process completion queue entries
         */
        uint32_t ProcessCompletionQueue(
            CompletionEntry* entries,
            uint32_t maxCount
        );
    };

}  // namespace RAON::Network::AsyncIO::Linux

#endif  // __linux__
