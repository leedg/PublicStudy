#pragma once

#include "AsyncIOProvider.h"

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>
#include <map>
#include <memory>
#include <mutex>

namespace Network::AsyncIO::Windows
{
    // =============================================================================
    // RIO (Registered I/O) based AsyncIOProvider Implementation
    // =============================================================================

    class RIOAsyncIOProvider : public AsyncIOProvider
    {
    public:
        RIOAsyncIOProvider();
        virtual ~RIOAsyncIOProvider();

        // Prevent copy
        RIOAsyncIOProvider(const RIOAsyncIOProvider&) = delete;
        RIOAsyncIOProvider& operator=(const RIOAsyncIOProvider&) = delete;

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
        // RIO Function Pointers
        // =====================================================================

        typedef int (WSAAPI *PfnRIOCloseCompletionQueue)(_In_ RIO_CQ cq);
        typedef RIO_CQ (WSAAPI *PfnRIOCreateCompletionQueue)(_In_ DWORD cqSize, _In_opt_ PRIO_NOTIFICATION_COMPLETION notificationCompletion);
        typedef int (WSAAPI *PfnRIOCreateRequestQueue)(_In_ SOCKET socket, _In_ DWORD maxOutstandingSend, _In_ DWORD maxOutstandingRecv, _In_ RIO_CQ cq);
        typedef int (WSAAPI *PfnRIODequeueCompletion)(_In_ RIO_CQ cq, _Out_writes_to_(arraySize, return) PRIO_RESULT array, _In_ DWORD arraySize);
        typedef void (WSAAPI *PfnRIONotify)(_In_ RIO_CQ cq);
        typedef RIO_BUFFERID (WSAAPI *PfnRIORegisterBuffer)(_In_ PCHAR dataBuffer, _In_ DWORD dataLength);
        typedef int (WSAAPI *PfnRIODeregisterBuffer)(_In_ RIO_BUFFERID bufferId);
        typedef int (WSAAPI *PfnRIOSend)(_In_ RIO_RQ requestQueue, _In_reads_(dataBufferCount) PRIO_BUF dataBuffers, _In_ DWORD dataBufferCount, _In_ DWORD flags, _In_ void *requestContext);
        typedef int (WSAAPI *PfnRIORecv)(_In_ RIO_RQ requestQueue, _In_reads_(dataBufferCount) PRIO_BUF dataBuffers, _In_ DWORD dataBufferCount, _In_ DWORD flags, _In_ void *requestContext);

        // =====================================================================
        // Internal Data Structures
        // =====================================================================

        struct RegisteredBuffer
        {
            RIO_BUFFERID rioBufferId;
            void* bufferPtr;
            uint32_t bufferSize;
            BufferPolicy policy;
        };

        struct PendingOperation
        {
            CompletionCallback callback;
            void* userData;
            SocketHandle socket;
            AsyncIOType operationType;
        };

        // =====================================================================
        // Member Variables
        // =====================================================================

        RIO_CQ mCompletionQueue;
        std::map<SocketHandle, RIO_RQ> mRequestQueues;
        std::map<int64_t, RegisteredBuffer> mRegisteredBuffers;
        std::map<void*, PendingOperation> mPendingOps;
        std::mutex mMutex;

        // RIO function pointers
        PfnRIOCloseCompletionQueue pfnRIOCloseCompletionQueue;
        PfnRIOCreateCompletionQueue pfnRIOCreateCompletionQueue;
        PfnRIOCreateRequestQueue pfnRIOCreateRequestQueue;
        PfnRIODequeueCompletion pfnRIODequeueCompletion;
        PfnRIONotify pfnRIONotify;
        PfnRIORegisterBuffer pfnRIORegisterBuffer;
        PfnRIODeregisterBuffer pfnRIODeregisterBuffer;
        PfnRIOSend pfnRIOSend;
        PfnRIORecv pfnRIORecv;

        uint32_t mMaxConcurrentOps;
        uint64_t mTotalSendOps;
        uint64_t mTotalRecvOps;
        uint64_t mTotalBytesTransferred;
        int64_t mNextBufferId;
        
        bool mInitialized;

        // =====================================================================
        // Helper Methods
        // =====================================================================

        /**
         * Load RIO function pointers from mswsock.dll
         */
        bool LoadRIOFunctions();

        /**
         * Convert RIO completion to CompletionEntry
         */
        bool ConvertRIOResult(
            const RIO_RESULT& rioResult,
            CompletionEntry& outEntry
        );
    };

}  // namespace Network::AsyncIO::Windows

#endif  // _WIN32
