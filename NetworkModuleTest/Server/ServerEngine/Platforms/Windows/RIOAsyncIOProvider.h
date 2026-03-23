#pragma once

// Windows 8+ 전용 RIO(Registered I/O) 기반 AsyncIOProvider 구현.
//
// RIO를 선택해야 하는 이유:
//   - IOCP는 WSASend/WSARecv 호출마다 커널이 버퍼를 non-paged pool에 pin한다.
//     연결이 많아지면 WSA 10055(WSAENOBUFS) 오류로 소켓을 닫아야 하는 상황이 발생한다.
//   - RIO는 VirtualAlloc 슬랩을 RIORegisterBuffer로 한 번만 등록하고 이후 I/O에서
//     재사용하므로 per-op 핀 비용과 WSAENOBUFS 오류를 원천 방지한다.
//   - 단, Windows 8 이상에서만 사용 가능하며 WSA_FLAG_REGISTERED_IO 소켓 플래그 필요.
//
// 슬랩 풀 설계:
//   - mRecvPool / mSendPool 각각 VirtualAlloc + 1회 RIORegisterBuffer.
//   - 슬롯 크기 8192 바이트, 슬롯 수 = maxConcurrent (연결당 1 recv + 1 send 슬롯).
//   - RIOBufferPool은 IBufferPool 인터페이스를 구현한 구체 클래스이며,
//     RIOBufferPool.h 에서 AsyncBufferPool의 using alias로도 제공된다.

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
	std::unordered_map<SocketHandle, RIO_RQ> mRequestQueues;           // socket → RIO 요청 큐 (O(1) 탐색)
	std::unordered_map<int64_t, RegisteredBufferEntry> mRegisteredBuffers; // bufferId → 등록 버퍼 (O(1) 탐색)
	std::unordered_map<uintptr_t, std::shared_ptr<PendingOperation>> mPendingOps; // opKey → 대기 작업 (O(1) 탐색)
	mutable std::mutex mMutex;

	// Pre-registered slab pools (mRecvPool / mSendPool own VirtualAlloc + 1x RIORegisterBuffer each)
	// 사전 등록 슬랩 풀 (각 풀이 VirtualAlloc + 1회 RIORegisterBuffer 보유)
	::Network::Core::Memory::RIOBufferPool mRecvPool;
	::Network::Core::Memory::RIOBufferPool mSendPool;
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
	mutable std::mutex mNotifyMutex; // RIONotify + 이벤트 대기를 한 스레드씩 직렬화
									 // (여러 워커가 동시에 notify를 arm하면 알림이 유실될 수 있다)
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
