#pragma once

// English: RIO (Registered I/O) based AsyncIOProvider implementation for
// Windows 8+

#include "Network/Core/AsyncIOProvider.h"

#ifdef _WIN32
#include "Core/Memory/RIOBufferPool.h"
#include <atomic>
#include <memory>
#include <mswsock.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

class RIOAsyncIOProvider : public AsyncIOProvider
{
  public:
	RIOAsyncIOProvider();
	virtual ~RIOAsyncIOProvider();

	RIOAsyncIOProvider(const RIOAsyncIOProvider &) = delete;
	RIOAsyncIOProvider &operator=(const RIOAsyncIOProvider &) = delete;

	AsyncIOError Initialize(size_t queueDepth, size_t maxConcurrent) override;
	void Shutdown() override;
	bool IsInitialized() const override;

	AsyncIOError AssociateSocket(SocketHandle socket,
								RequestContext context) override;

	int64_t RegisterBuffer(const void *ptr, size_t size) override;
	AsyncIOError UnregisterBuffer(int64_t bufferId) override;

	AsyncIOError SendAsync(SocketHandle socket, const void *buffer, size_t size,
						   RequestContext context, uint32_t flags = 0) override;

	AsyncIOError RecvAsync(SocketHandle socket, void *buffer, size_t size,
						   RequestContext context, uint32_t flags = 0) override;

	AsyncIOError FlushRequests() override;

	int ProcessCompletions(CompletionEntry *entries, size_t maxEntries,
					   int timeoutMs = 0) override;

	const ProviderInfo &GetInfo() const override;
	ProviderStats GetStats() const override;
	const char *GetLastError() const override;

  private:
	typedef void(WSAAPI *PfnRIOCloseCompletionQueue)(_In_ RIO_CQ cq);
	typedef RIO_CQ(WSAAPI *PfnRIOCreateCompletionQueue)(
		_In_ DWORD cqSize,
		_In_opt_ PRIO_NOTIFICATION_COMPLETION notificationCompletion);
	typedef RIO_RQ(WSAAPI *PfnRIOCreateRequestQueue)(
		_In_ SOCKET socket, _In_ ULONG maxOutstandingReceive,
		_In_ ULONG maxReceiveDataBuffers, _In_ ULONG maxOutstandingSend,
		_In_ ULONG maxSendDataBuffers, _In_ RIO_CQ receiveCQ,
		_In_ RIO_CQ sendCQ, _In_opt_ void *socketContext);
	typedef ULONG(WSAAPI *PfnRIODequeueCompletion)(
		_In_ RIO_CQ cq, _Out_writes_to_(arraySize, return) PRIORESULT array,
		_In_ ULONG arraySize);
	typedef BOOL(WSAAPI *PfnRIONotify)(_In_ RIO_CQ cq);
	typedef RIO_BUFFERID(WSAAPI *PfnRIORegisterBuffer)(_In_ PCHAR dataBuffer,
										   _In_ DWORD dataLength);
	typedef void(WSAAPI *PfnRIODeregisterBuffer)(_In_ RIO_BUFFERID bufferId);
	typedef BOOL(WSAAPI *PfnRIOSend)(_In_ RIO_RQ requestQueue,
								 _In_reads_(dataBufferCount) PRIO_BUF dataBuffers,
								 _In_ DWORD dataBufferCount, _In_ DWORD flags,
								 _In_ void *requestContext);
	typedef BOOL(WSAAPI *PfnRIORecv)(_In_ RIO_RQ requestQueue,
								 _In_reads_(dataBufferCount) PRIO_BUF dataBuffers,
								 _In_ DWORD dataBufferCount, _In_ DWORD flags,
								 _In_ void *requestContext);

	struct RegisteredBufferEntry
	{
		RIO_BUFFERID mRioBufferId;
		void *mBufferPtr;
		uint32_t mBufferSize;
	};

	struct PendingOperation
	{
		RequestContext mContext = 0;
		uintptr_t mOpId = 0;
		SocketHandle mSocket = INVALID_SOCKET;
		AsyncIOType mType = AsyncIOType::Recv;
		void *mBufferPtr = nullptr;   // recv: session buffer for post-completion copy
		size_t mBufferSize = 0;
		size_t mSendSlotIdx = SIZE_MAX; // send: slab slot index to return on completion
	};

	RIO_CQ mCompletionQueue;
	std::unordered_map<SocketHandle, RIO_RQ> mRequestQueues;           // English: O(1) request queue lookup / 한글: O(1) 요청 큐 탐색
	std::unordered_map<int64_t, RegisteredBufferEntry> mRegisteredBuffers; // English: O(1) buffer lookup / 한글: O(1) 버퍼 탐색
	std::unordered_map<uintptr_t, std::shared_ptr<PendingOperation>> mPendingOps; // English: O(1) pending op lookup / 한글: O(1) 대기 작업 탐색
	mutable std::mutex mMutex;

	// Pre-registered slab pools (mRecvPool / mSendPool own VirtualAlloc + 1x RIORegisterBuffer each)
	// 사전 등록 슬랩 풀 (각 풀이 VirtualAlloc + 1회 RIORegisterBuffer 보유)
	::Core::Memory::RIOBufferPool mRecvPool;
	::Core::Memory::RIOBufferPool mSendPool;
	std::unordered_map<SocketHandle, size_t> mSocketRecvSlot; // guarded by mMutex

	PfnRIOCloseCompletionQueue mPfnRIOCloseCompletionQueue;
	PfnRIOCreateCompletionQueue mPfnRIOCreateCompletionQueue;
	PfnRIOCreateRequestQueue mPfnRIOCreateRequestQueue;
	PfnRIODequeueCompletion mPfnRIODequeueCompletion;
	PfnRIONotify mPfnRIONotify;
	PfnRIORegisterBuffer mPfnRIORegisterBuffer;
	PfnRIODeregisterBuffer mPfnRIODeregisterBuffer;
	PfnRIOSend mPfnRIOSend;
	PfnRIORecv mPfnRIORecv;

	HANDLE mCompletionEvent;
	mutable std::mutex mNotifyMutex; // English: Serializes RIONotify + event wait to one thread at a time
									 // 한글: RIONotify + 이벤트 대기를 한 스레드씩 직렬화
	ProviderInfo mInfo;
	ProviderStats mStats;
	std::string mLastError;
	size_t mMaxConcurrentOps;
	int64_t mNextBufferId;
	std::atomic<uint64_t> mNextOpId{1};
	std::atomic<bool> mInitialized;
	std::atomic<bool> mShuttingDown{false};

	bool LoadRIOFunctions();
	AsyncIOError GetOrCreateRequestQueue(SocketHandle socket, RIO_RQ &outQueue,
									 RequestContext contextForSocket);
	void CleanupPendingOperation(PendingOperation &op);
};

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
