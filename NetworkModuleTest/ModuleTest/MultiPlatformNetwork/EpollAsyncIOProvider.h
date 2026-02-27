#pragma once

// English: epoll-based AsyncIOProvider implementation for Linux
// ?쒓?: Linux??epoll 湲곕컲 AsyncIOProvider 援ы쁽

#include "AsyncIOProvider.h"

#ifdef __linux__
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <sys/epoll.h>

namespace Network
{
namespace AsyncIO
{
namespace Linux
{
// =============================================================================
// English: epoll-based AsyncIOProvider Implementation
// ?쒓?: epoll 湲곕컲 AsyncIOProvider 援ы쁽
// =============================================================================

class EpollAsyncIOProvider : public AsyncIOProvider
{
  public:
	// English: Constructor
	// ?쒓?: ?앹꽦??
	EpollAsyncIOProvider();

	// English: Destructor - releases epoll resources
	// ?쒓?: ?뚮㈇??- epoll 由ъ냼???댁젣
	virtual ~EpollAsyncIOProvider();

	// English: Prevent copy (move-only semantics)
	// ?쒓?: 蹂듭궗 諛⑹? (move-only ?섎?濡?
	EpollAsyncIOProvider(const EpollAsyncIOProvider &) = delete;
	EpollAsyncIOProvider &operator=(const EpollAsyncIOProvider &) = delete;

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

	// English: Pending operation tracking structure
	// ?쒓?: ?湲?以묒씤 ?묒뾽 異붿쟻 援ъ“泥?
	struct PendingOperation
	{
		RequestContext mContext; // English: User request context / ?쒓?:
								 // ?ъ슜???붿껌 而⑦뀓?ㅽ듃
		AsyncIOType mType; // English: Operation type / ?쒓?: ?묒뾽 ???
		std::unique_ptr<uint8_t[]> mBuffer; // English: Dynamically allocated
											// buffer / ?쒓?: ?숈쟻 ?좊떦 踰꾪띁
		uint32_t mBufferSize; // English: Buffer size / ?쒓?: 踰꾪띁 ?ш린
	};

	// =====================================================================
	// English: Member Variables
	// ?쒓?: 硫ㅻ쾭 蹂??
	// =====================================================================

	int mEpollFd; // English: epoll file descriptor / ?쒓?: epoll ?뚯씪
				  // ?붿뒪?щ┰??
	std::map<SocketHandle, PendingOperation>
		mPendingOps; // English: Pending operations / ?쒓?: ?湲??묒뾽
	mutable std::mutex
		mMutex; // English: Thread safety mutex / ?쒓?: ?ㅻ젅???덉쟾??裕ㅽ뀓??
	ProviderInfo mInfo;     // English: Provider info / ?쒓?: 怨듦툒???뺣낫
	ProviderStats mStats;   // English: Statistics / ?쒓?: ?듦퀎
	std::string mLastError; // English: Last error message / ?쒓?: 留덉?留??먮윭
							// 硫붿떆吏
	size_t mMaxConcurrentOps; // English: Max concurrent ops / ?쒓?: 理쒕? ?숈떆
								  // ?묒뾽
	bool mInitialized; // English: Initialization flag / ?쒓?: 珥덇린???뚮옒洹?
};

} // namespace Network::AsyncIO::Linux

#endif // __linux__
