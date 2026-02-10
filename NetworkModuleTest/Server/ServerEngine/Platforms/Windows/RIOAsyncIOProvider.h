#pragma once

// English: RIO (Registered I/O) based AsyncIOProvider implementation for
// Windows 8+

#include "Network/Core/AsyncIOProvider.h"

#ifdef _WIN32
#include <map>
#include <memory>
#include <mswsock.h>
#include <mutex>
#include <string>
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
		SocketHandle mSocket = INVALID_SOCKET;
		AsyncIOType mType = AsyncIOType::Recv;
		RIO_BUFFERID mRioBufferId = RIO_INVALID_BUFFERID;
		void *mBufferPtr = nullptr;
		size_t mBufferSize = 0;
		std::vector<uint8_t> mOwnedBuffer;
	};

	RIO_CQ mCompletionQueue;
	std::map<SocketHandle, RIO_RQ> mRequestQueues;
	std::map<int64_t, RegisteredBufferEntry> mRegisteredBuffers;
	std::map<void *, std::unique_ptr<PendingOperation>> mPendingOps;
	mutable std::mutex mMutex;

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
	ProviderInfo mInfo;
	ProviderStats mStats;
	std::string mLastError;
	size_t mMaxConcurrentOps;
	int64_t mNextBufferId;
	bool mInitialized;

	bool LoadRIOFunctions();
	AsyncIOError GetOrCreateRequestQueue(SocketHandle socket, RIO_RQ &outQueue,
									 RequestContext contextForSocket);
	void CleanupPendingOperation(std::unique_ptr<PendingOperation> &op);
};

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
