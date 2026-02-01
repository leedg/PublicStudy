#pragma once

// English: IOCP-based AsyncIOProvider implementation for Windows
// ?쒓?: Windows??IOCP 湲곕컲 AsyncIOProvider 援ы쁽

#include "AsyncIOProvider.h"

#ifdef _WIN32
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace Network {
namespace AsyncIO {
namespace Windows {
    // =============================================================================
    // English: IOCP-based AsyncIOProvider Implementation
    // ?쒓?: IOCP 湲곕컲 AsyncIOProvider 援ы쁽
    // =============================================================================

    class IocpAsyncIOProvider : public AsyncIOProvider
    {
    public:
        // English: Constructor
        // ?쒓?: ?앹꽦??
        IocpAsyncIOProvider();

        // English: Destructor - releases IOCP resources
        // ?쒓?: ?뚮㈇??- IOCP 由ъ냼???댁젣
        virtual ~IocpAsyncIOProvider();

        // English: Prevent copy (move-only semantics)
        // ?쒓?: 蹂듭궗 諛⑹? (move-only ?섎?濡?
        IocpAsyncIOProvider(const IocpAsyncIOProvider&) = delete;
        IocpAsyncIOProvider& operator=(const IocpAsyncIOProvider&) = delete;

        // =====================================================================
        // English: Lifecycle Management
        // ?쒓?: ?앸챸二쇨린 愿由?
        // =====================================================================

        AsyncIOError Initialize(size_t queueDepth, size_t maxConcurrent) override;
        void Shutdown() override;
        bool IsInitialized() const override;

        // =====================================================================
        // English: Buffer Management
        // ?쒓?: 踰꾪띁 愿由?
        // =====================================================================

        int64_t RegisterBuffer(const void* ptr, size_t size) override;
        AsyncIOError UnregisterBuffer(int64_t bufferId) override;

        // =====================================================================
        // English: Async I/O Requests
        // ?쒓?: 鍮꾨룞湲?I/O ?붿껌
        // =====================================================================

        AsyncIOError SendAsync(
            SocketHandle socket,
            const void* buffer,
            size_t size,
            RequestContext context,
            uint32_t flags = 0
        ) override;

        AsyncIOError RecvAsync(
            SocketHandle socket,
            void* buffer,
            size_t size,
            RequestContext context,
            uint32_t flags = 0
        ) override;

        AsyncIOError FlushRequests() override;

        // =====================================================================
        // English: Completion Processing
        // ?쒓?: ?꾨즺 泥섎━
        // =====================================================================

        int ProcessCompletions(
            CompletionEntry* entries,
            size_t maxEntries,
            int timeoutMs = 0
        ) override;

        // =====================================================================
        // English: Information & Statistics
        // ?쒓?: ?뺣낫 諛??듦퀎
        // =====================================================================

        const ProviderInfo& GetInfo() const override;
        ProviderStats GetStats() const override;
        const char* GetLastError() const override;

    private:
        // =====================================================================
        // English: Internal Data Structures
        // ?쒓?: ?대? ?곗씠??援ъ“
        // =====================================================================

        // English: Pending operation tracking structure
        // ?쒓?: ?湲?以묒씤 ?묒뾽 異붿쟻 援ъ“泥?
        struct PendingOperation
        {
            OVERLAPPED mOverlapped;              // English: IOCP overlapped structure / ?쒓?: IOCP ?ㅻ쾭??援ъ“泥?
            WSABUF mWsaBuffer;                   // English: WSA buffer / ?쒓?: WSA 踰꾪띁
            std::unique_ptr<uint8_t[]> mBuffer;  // English: Dynamically allocated buffer / ?쒓?: ?숈쟻 ?좊떦 踰꾪띁
            RequestContext mContext;              // English: User request context / ?쒓?: ?ъ슜???붿껌 而⑦뀓?ㅽ듃
            AsyncIOType mType;                   // English: Operation type / ?쒓?: ?묒뾽 ???
        };

        // =====================================================================
        // English: Member Variables
        // ?쒓?: 硫ㅻ쾭 蹂??
        // =====================================================================

        HANDLE mCompletionPort;                  // English: IOCP completion port handle / ?쒓?: IOCP ?꾨즺 ?ы듃 ?몃뱾
        std::map<SocketHandle, std::unique_ptr<PendingOperation>> mPendingOps;  // English: Pending ops / ?쒓?: ?湲??묒뾽
        mutable std::mutex mMutex;               // English: Thread safety mutex / ?쒓?: ?ㅻ젅???덉쟾??裕ㅽ뀓??
        ProviderInfo mInfo;                      // English: Provider info cache / ?쒓?: 怨듦툒???뺣낫 罹먯떆
        ProviderStats mStats;                    // English: Statistics / ?쒓?: ?듦퀎
        std::string mLastError;                  // English: Last error message / ?쒓?: 留덉?留??먮윭 硫붿떆吏
        size_t mMaxConcurrentOps;                // English: Max concurrent ops / ?쒓?: 理쒕? ?숈떆 ?묒뾽 ??
        bool mInitialized;                       // English: Initialization flag / ?쒓?: 珥덇린???뚮옒洹?
    };

}  // namespace Windows
}  // namespace AsyncIO
}  // namespace Network

#endif  // _WIN32

