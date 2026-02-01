#pragma once

// English: kqueue-based AsyncIOProvider implementation for macOS/BSD
// ?쒓?: macOS/BSD??kqueue 湲곕컲 AsyncIOProvider 援ы쁽

#include "AsyncIOProvider.h"

#ifdef __APPLE__
#include <sys/event.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace Network {
namespace AsyncIO {
namespace BSD {
    // =============================================================================
    // English: kqueue-based AsyncIOProvider Implementation (macOS/BSD)
    // ?쒓?: kqueue 湲곕컲 AsyncIOProvider 援ы쁽 (macOS/BSD)
    // =============================================================================

    class KqueueAsyncIOProvider : public AsyncIOProvider
    {
    public:
        // English: Constructor
        // ?쒓?: ?앹꽦??
        KqueueAsyncIOProvider();

        // English: Destructor - releases kqueue resources
        // ?쒓?: ?뚮㈇??- kqueue 由ъ냼???댁젣
        virtual ~KqueueAsyncIOProvider();

        // English: Prevent copy (move-only semantics)
        // ?쒓?: 蹂듭궗 諛⑹? (move-only ?섎?濡?
        KqueueAsyncIOProvider(const KqueueAsyncIOProvider&) = delete;
        KqueueAsyncIOProvider& operator=(const KqueueAsyncIOProvider&) = delete;

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

        // English: Pending operation tracking
        // ?쒓?: ?湲??묒뾽 異붿쟻
        struct PendingOperation
        {
            RequestContext mContext;              // English: User request context / ?쒓?: ?ъ슜???붿껌 而⑦뀓?ㅽ듃
            AsyncIOType mType;                   // English: Operation type / ?쒓?: ?묒뾽 ???
            SocketHandle mSocket;                // English: Socket handle / ?쒓?: ?뚯폆 ?몃뱾
            std::unique_ptr<uint8_t[]> mBuffer;  // English: Buffer / ?쒓?: 踰꾪띁
            uint32_t mBufferSize;                // English: Buffer size / ?쒓?: 踰꾪띁 ?ш린
        };

        // =====================================================================
        // English: Member Variables
        // ?쒓?: 硫ㅻ쾭 蹂??
        // =====================================================================

        int mKqueueFd;                           // English: kqueue file descriptor / ?쒓?: kqueue ?뚯씪 ?붿뒪?щ┰??
        std::map<SocketHandle, PendingOperation> mPendingOps;  // English: Pending ops / ?쒓?: ?湲??묒뾽
        std::map<SocketHandle, bool> mRegisteredSockets;  // English: Registered sockets / ?쒓?: ?깅줉???뚯폆
        mutable std::mutex mMutex;               // English: Thread safety mutex / ?쒓?: ?ㅻ젅???덉쟾??裕ㅽ뀓??
        ProviderInfo mInfo;                      // English: Provider info / ?쒓?: 怨듦툒???뺣낫
        ProviderStats mStats;                    // English: Statistics / ?쒓?: ?듦퀎
        std::string mLastError;                  // English: Last error message / ?쒓?: 留덉?留??먮윭 硫붿떆吏
        size_t mMaxConcurrentOps;                // English: Max concurrent ops / ?쒓?: 理쒕? ?숈떆 ?묒뾽
        bool mInitialized;                       // English: Initialization flag / ?쒓?: 珥덇린???뚮옒洹?

        // =====================================================================
        // English: Helper Methods
        // ?쒓?: ?ы띁 硫붿꽌??
        // =====================================================================

        // English: Register socket with kqueue for read and write events
        // ?쒓?: kqueue???뚯폆???쎄린/?곌린 ?대깽?몃줈 ?깅줉
        bool RegisterSocketEvents(SocketHandle socket);

        // English: Unregister socket events from kqueue
        // ?쒓?: kqueue?먯꽌 ?뚯폆 ?대깽???깅줉 ?댁젣
        bool UnregisterSocketEvents(SocketHandle socket);
    };

}  // namespace Network::AsyncIO::BSD

#endif  // __APPLE__

