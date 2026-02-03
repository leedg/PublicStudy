#pragma once

// English: RIO (Registered I/O) based AsyncIOProvider implementation for
// Windows 8+ ?쒓?: Windows 8+ ??RIO (?깅줉 I/O) 湲곕컲 AsyncIOProvider 援ы쁽

#include "AsyncIOProvider.h"

#ifdef _WIN32
#include <map>
#include <memory>
#include <mswsock.h>
#include <mutex>
#include <string>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{
// =============================================================================
// English: RIO (Registered I/O) based AsyncIOProvider Implementation
// ?쒓?: RIO (?깅줉 I/O) 湲곕컲 AsyncIOProvider 援ы쁽
// =============================================================================

class RIOAsyncIOProvider : public AsyncIOProvider
{
  public:
	// English: Constructor
	// ?쒓?: ?앹꽦??
	RIOAsyncIOProvider();

	// English: Destructor - releases RIO resources
	// ?쒓?: ?뚮㈇??- RIO 由ъ냼???댁젣
	virtual ~RIOAsyncIOProvider();

	// English: Prevent copy (move-only semantics)
	// ?쒓?: 蹂듭궗 諛⑹? (move-only ?섎?濡?
	RIOAsyncIOProvider(const RIOAsyncIOProvider &) = delete;
	RIOAsyncIOProvider &operator=(const RIOAsyncIOProvider &) = delete;

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
	// English: RIO Function Pointer Types
	// ?쒓?: RIO ?⑥닔 ?ъ씤?????
	// =====================================================================

	typedef int(WSAAPI *PfnRIOCloseCompletionQueue)(_In_ RIO_CQ cq);
	typedef RIO_CQ(WSAAPI *PfnRIOCreateCompletionQueue)(
		_In_ DWORD cqSize,
		_In_opt_ PRIO_NOTIFICATION_COMPLETION notificationCompletion);
	typedef RIO_RQ(WSAAPI *PfnRIOCreateRequestQueue)(
		_In_ SOCKET socket, _In_ DWORD maxOutstandingSend,
		_In_ DWORD maxOutstandingRecv, _In_ RIO_CQ cq);
	typedef ULONG(WSAAPI *PfnRIODequeueCompletion)(
		_In_ RIO_CQ cq, _Out_writes_to_(arraySize, return) PRIORESULT array,
		_In_ ULONG arraySize);
	typedef void(WSAAPI *PfnRIONotify)(_In_ RIO_CQ cq);
	typedef RIO_BUFFERID(WSAAPI *PfnRIORegisterBuffer)(_In_ PCHAR dataBuffer,
														   _In_ DWORD dataLength);
	typedef int(WSAAPI *PfnRIODeregisterBuffer)(_In_ RIO_BUFFERID bufferId);
	typedef int(WSAAPI *PfnRIOSend)(_In_ RIO_RQ requestQueue,
									_In_reads_(dataBufferCount)
										PRIO_BUF dataBuffers,
									_In_ DWORD dataBufferCount,
									_In_ DWORD flags,
									_In_ void *requestContext);
	typedef int(WSAAPI *PfnRIORecv)(_In_ RIO_RQ requestQueue,
									_In_reads_(dataBufferCount)
										PRIO_BUF dataBuffers,
									_In_ DWORD dataBufferCount,
									_In_ DWORD flags,
									_In_ void *requestContext);

	// =====================================================================
	// English: Internal Data Structures
	// ?쒓?: ?대? ?곗씠??援ъ“
	// =====================================================================

	// English: Registered buffer info
	// ?쒓?: ?깅줉??踰꾪띁 ?뺣낫
	struct RegisteredBufferEntry
	{
		RIO_BUFFERID
		mRioBufferId;         // English: RIO buffer ID / ?쒓?: RIO 踰꾪띁 ID
		void *mBufferPtr;     // English: Buffer pointer / ?쒓?: 踰꾪띁 ?ъ씤??
		uint32_t mBufferSize; // English: Buffer size / ?쒓?: 踰꾪띁 ?ш린
	};

	// English: Pending operation tracking
	// ?쒓?: ?湲??묒뾽 異붿쟻
	struct PendingOperation
	{
		RequestContext mContext; // English: User request context / ?쒓?:
								 // ?ъ슜???붿껌 而⑦뀓?ㅽ듃
		SocketHandle mSocket;    // English: Socket handle / ?쒓?: ?뚯폆 ?몃뱾
		AsyncIOType mType; // English: Operation type / ?쒓?: ?묒뾽 ???
	};

	// =====================================================================
	// English: Member Variables
	// ?쒓?: 硫ㅻ쾭 蹂??
	// =====================================================================

	RIO_CQ
	mCompletionQueue; // English: RIO completion queue / ?쒓?: RIO ?꾨즺 ??
	std::map<SocketHandle, RIO_RQ>
		mRequestQueues; // English: Request queues per socket / ?쒓?:
						// ?뚯폆蹂??붿껌 ??
	std::map<int64_t, RegisteredBufferEntry>
		mRegisteredBuffers; // English: Registered buffers / ?쒓?: ?깅줉??踰꾪띁
	std::map<void *, PendingOperation>
		mPendingOps; // English: Pending operations / ?쒓?: ?湲??묒뾽
	mutable std::mutex
		mMutex; // English: Thread safety mutex / ?쒓?: ?ㅻ젅???덉쟾??裕ㅽ뀓??

	// English: RIO function pointers (mVariableName convention)
	// ?쒓?: RIO ?⑥닔 ?ъ씤??(mVariableName 洹쒖튃)
	PfnRIOCloseCompletionQueue mPfnRIOCloseCompletionQueue;
	PfnRIOCreateCompletionQueue mPfnRIOCreateCompletionQueue;
	PfnRIOCreateRequestQueue mPfnRIOCreateRequestQueue;
	PfnRIODequeueCompletion mPfnRIODequeueCompletion;
	PfnRIONotify mPfnRIONotify;
	PfnRIORegisterBuffer mPfnRIORegisterBuffer;
	PfnRIODeregisterBuffer mPfnRIODeregisterBuffer;
	PfnRIOSend mPfnRIOSend;
	PfnRIORecv mPfnRIORecv;

	ProviderInfo mInfo;     // English: Provider info / ?쒓?: 怨듦툒???뺣낫
	ProviderStats mStats;   // English: Statistics / ?쒓?: ?듦퀎
	std::string mLastError; // English: Last error message / ?쒓?: 留덉?留??먮윭
							// 硫붿떆吏
	size_t mMaxConcurrentOps; // English: Max concurrent ops / ?쒓?: 理쒕? ?숈떆
								  // ?묒뾽
	int64_t mNextBufferId;    // English: Next buffer ID / ?쒓?: ?ㅼ쓬 踰꾪띁 ID
	bool mInitialized; // English: Initialization flag / ?쒓?: 珥덇린???뚮옒洹?

	// =====================================================================
	// English: Helper Methods
	// ?쒓?: ?ы띁 硫붿꽌??
	// =====================================================================

	// English: Load RIO function pointers from mswsock.dll
	// ?쒓?: mswsock.dll?먯꽌 RIO ?⑥닔 ?ъ씤??濡쒕뱶
	bool LoadRIOFunctions();
};

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
