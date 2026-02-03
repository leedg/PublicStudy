#pragma once

// English: io_uring-based AsyncIOProvider implementation for Linux kernel 5.1+
// ?쒓?: Linux 而ㅻ꼸 5.1+ ??io_uring 湲곕컲 AsyncIOProvider 援ы쁽

#include "AsyncIOProvider.h"

#ifdef __linux__
#include <liburing.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace Network::AsyncIO::Linux
{
// =============================================================================
// English: io_uring-based AsyncIOProvider Implementation (Linux kernel 5.1+)
// ?쒓?: io_uring 湲곕컲 AsyncIOProvider 援ы쁽 (Linux 而ㅻ꼸 5.1+)
// =============================================================================

class IOUringAsyncIOProvider : public AsyncIOProvider
{
  public:
	// English: Constructor
	// ?쒓?: ?앹꽦??
	IOUringAsyncIOProvider();

	// English: Destructor - releases io_uring resources
	// ?쒓?: ?뚮㈇??- io_uring 由ъ냼???댁젣
	virtual ~IOUringAsyncIOProvider();

	// English: Prevent copy (move-only semantics)
	// ?쒓?: 蹂듭궗 諛⑹? (move-only ?섎?濡?
	IOUringAsyncIOProvider(const IOUringAsyncIOProvider &) = delete;
	IOUringAsyncIOProvider &operator=(const IOUringAsyncIOProvider &) = delete;

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

	int64_t RegisterBuffer(const void *ptr, size_t size) override;
	AsyncIOError UnregisterBuffer(int64_t bufferId) override;

	// =====================================================================
	// English: Async I/O Requests
	// ?쒓?: 鍮꾨룞湲?I/O ?붿껌
	// =====================================================================

	AsyncIOError SendAsync(SocketHandle socket, const void *buffer, size_t size,
							   RequestContext context, uint32_t flags = 0) override;

	AsyncIOError RecvAsync(SocketHandle socket, void *buffer, size_t size,
							   RequestContext context, uint32_t flags = 0) override;

	AsyncIOError FlushRequests() override;

	// =====================================================================
	// English: Completion Processing
	// ?쒓?: ?꾨즺 泥섎━
	// =====================================================================

	int ProcessCompletions(CompletionEntry *entries, size_t maxEntries,
							   int timeoutMs = 0) override;

	// =====================================================================
	// English: Information & Statistics
	// ?쒓?: ?뺣낫 諛??듦퀎
	// =====================================================================

	const ProviderInfo &GetInfo() const override;
	ProviderStats GetStats() const override;
	const char *GetLastError() const override;

  private:
	// =====================================================================
	// English: Internal Data Structures
	// ?쒓?: ?대? ?곗씠??援ъ“
	// =====================================================================

	// English: Pending operation tracking
	// ?쒓?: ?湲??묒뾽 異붿쟻
	struct PendingOperation
	{
		RequestContext mContext; // English: User request context / ?쒓?:
								 // ?ъ슜???붿껌 而⑦뀓?ㅽ듃
		AsyncIOType mType;    // English: Operation type / ?쒓?: ?묒뾽 ???
		SocketHandle mSocket; // English: Socket handle / ?쒓?: ?뚯폆 ?몃뱾
		std::unique_ptr<uint8_t[]> mBuffer; // English: Dynamically allocated
											// buffer / ?쒓?: ?숈쟻 ?좊떦 踰꾪띁
		uint32_t mBufferSize; // English: Buffer size / ?쒓?: 踰꾪띁 ?ш린
	};

	// English: Registered buffer info
	// ?쒓?: ?깅줉??踰꾪띁 ?뺣낫
	struct RegisteredBufferEntry
	{
		void *mAddress; // English: Buffer address / ?쒓?: 踰꾪띁 二쇱냼
		uint32_t mSize; // English: Buffer size / ?쒓?: 踰꾪띁 ?ш린
		int32_t
			mBufferGroupId; // English: Buffer group ID / ?쒓?: 踰꾪띁 洹몃９ ID
	};

	// =====================================================================
	// English: Member Variables
	// ?쒓?: 硫ㅻ쾭 蹂??
	// =====================================================================

	io_uring mRing; // English: io_uring ring / ?쒓?: io_uring 留?
	std::map<uint64_t, PendingOperation>
		mPendingOps; // English: Pending ops by user_data / ?쒓?:
					 // user_data蹂??湲??묒뾽
	std::map<int64_t, RegisteredBufferEntry>
		mRegisteredBuffers; // English: Registered buffers / ?쒓?: ?깅줉??踰꾪띁
	mutable std::mutex
		mMutex; // English: Thread safety mutex / ?쒓?: ?ㅻ젅???덉쟾??裕ㅽ뀓??
	ProviderInfo mInfo;     // English: Provider info / ?쒓?: 怨듦툒???뺣낫
	ProviderStats mStats;   // English: Statistics / ?쒓?: ?듦퀎
	std::string mLastError; // English: Last error message / ?쒓?: 留덉?留??먮윭
							// 硫붿떆吏
	size_t mMaxConcurrentOps; // English: Max concurrent ops / ?쒓?: 理쒕? ?숈떆
								  // ?묒뾽
	int64_t mNextBufferId;    // English: Next buffer ID / ?쒓?: ?ㅼ쓬 踰꾪띁 ID
	uint64_t mNextOpKey; // English: Next operation key / ?쒓?: ?ㅼ쓬 ?묒뾽 ??
	bool mInitialized; // English: Initialization flag / ?쒓?: 珥덇린???뚮옒洹?
	bool mSupportsFixedBuffers; // English: Fixed buffer support / ?쒓?: 怨좎젙
								// 踰꾪띁 吏??
	bool mSupportsDirectDescriptors; // English: Direct descriptor support /
									 // ?쒓?: 吏곸젒 ?붿뒪?щ┰??吏??

	// =====================================================================
	// English: Helper Methods
	// ?쒓?: ?ы띁 硫붿꽌??
	// =====================================================================

	// English: Submit pending operations to the ring
	// ?쒓?: ?湲??묒뾽??留곸뿉 ?쒖텧
	bool SubmitRing();

	// English: Process completion queue entries
	// ?쒓?: ?꾨즺 ????ぉ 泥섎━
	int ProcessCompletionQueue(CompletionEntry *entries, size_t maxEntries);
};

} // namespace Linux
} // namespace AsyncIO
} // namespace Network

#endif // __linux__
