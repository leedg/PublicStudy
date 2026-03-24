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
	// ─────────────────────────────────────────────
	// RIO API 함수 포인터 타입 정의 (Windows 전용)
	// LoadRIOFunctions()에서 WSAIoctl로 동적 로드
	// ─────────────────────────────────────────────
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

	// ─────────────────────────────────────────────
	// 내부 데이터 구조
	// ─────────────────────────────────────────────

	// RegisterBuffer()로 외부 등록된 버퍼 항목 (RegisteredBuffers 맵 값)
	struct RegisteredBufferEntry
	{
		RIO_BUFFERID mRioBufferId; // RIORegisterBuffer가 반환한 등록 ID
		void        *mBufferPtr;   // 등록된 버퍼의 시작 주소
		uint32_t     mBufferSize;  // 등록 크기 (바이트); DWORD 범위 내로 제한
	};

	// 진행 중인 RIO 비동기 I/O 작업 추적 구조체
	struct PendingOperation
	{
		RequestContext mContext    = 0;                   // ConnectionId — 완료 시 세션 조회에 사용
		uintptr_t      mOpId      = 0;                   // 고유 작업 키 (mNextOpId에서 할당)
		SocketHandle   mSocket    = INVALID_SOCKET;      // 작업을 발행한 소켓
		AsyncIOType    mType      = AsyncIOType::Recv;   // I/O 방향 (Recv 또는 Send)
		void          *mBufferPtr = nullptr;             // recv: 완료 후 memcpy 목적지(세션 버퍼)
		size_t         mBufferSize = 0;                  // 버퍼 크기 (바이트)
		size_t         mSendSlotIdx = SIZE_MAX;          // send: 완료/실패 시 반환할 슬랩 슬롯 인덱스
	};

	// ─────────────────────────────────────────────
	// RIO 완료 큐 및 소켓-큐 맵
	// ─────────────────────────────────────────────
	RIO_CQ mCompletionQueue; // RIO 완료 큐 핸들 — 모든 소켓의 recv/send 완료를 수신 (Windows RIO 전용)

	std::unordered_map<SocketHandle, RIO_RQ>    mRequestQueues;    // socket → RIO 소켓별 요청 큐 (O(1) 탐색); mMutex로 보호
	std::unordered_map<int64_t, RegisteredBufferEntry> mRegisteredBuffers; // bufferId → 외부 등록 버퍼 (O(1) 탐색); mMutex로 보호
	std::unordered_map<uintptr_t, std::shared_ptr<PendingOperation>> mPendingOps; // opKey → 대기 작업 (O(1) 탐색); mMutex로 보호

	mutable std::mutex mMutex; // 위 맵들과 mStats를 직렬화하는 뮤텍스

	// ─────────────────────────────────────────────
	// 사전 등록 슬랩 풀 (Windows RIO 전용)
	// 각 풀이 VirtualAlloc + 1회 RIORegisterBuffer 보유
	// ─────────────────────────────────────────────
	::Network::Core::Memory::RIOBufferPool   mRecvPool;       // recv용 사전 등록 슬랩 풀
	::Network::Core::Memory::RIOBufferPool   mSendPool;       // send용 사전 등록 슬랩 풀
	std::unordered_map<SocketHandle, size_t> mSocketRecvSlot; // socket → recv 슬랩 슬롯 인덱스; mMutex로 보호

	// ─────────────────────────────────────────────
	// RIO API 함수 포인터 (LoadRIOFunctions로 초기화)
	// ─────────────────────────────────────────────
	PfnRIOCloseCompletionQueue  mPfnRIOCloseCompletionQueue;  // RIOCloseCompletionQueue
	PfnRIOCreateCompletionQueue mPfnRIOCreateCompletionQueue; // RIOCreateCompletionQueue
	PfnRIOCreateRequestQueue    mPfnRIOCreateRequestQueue;    // RIOCreateRequestQueue
	PfnRIODequeueCompletion     mPfnRIODequeueCompletion;     // RIODequeueCompletion
	PfnRIONotify                mPfnRIONotify;                // RIONotify
	PfnRIORegisterBuffer        mPfnRIORegisterBuffer;        // RIORegisterBuffer
	PfnRIODeregisterBuffer      mPfnRIODeregisterBuffer;      // RIODeregisterBuffer
	PfnRIOSend                  mPfnRIOSend;                  // RIOSend
	PfnRIORecv                  mPfnRIORecv;                  // RIOReceive

	// ─────────────────────────────────────────────
	// 완료 이벤트 및 알림 직렬화
	// ─────────────────────────────────────────────
	HANDLE             mCompletionEvent; // RIO 완료 알림 이벤트 — RIO_EVENT_COMPLETION 방식 (Windows 전용)
	mutable std::mutex mNotifyMutex;     // RIONotify + 이벤트 대기를 한 스레드씩 직렬화
	                                     // (여러 워커가 동시에 notify를 arm하면 알림이 유실될 수 있다)

	// ─────────────────────────────────────────────
	// 상태 및 통계
	// ─────────────────────────────────────────────
	ProviderInfo          mInfo;             // 플랫폼 정보 (이름, 지원 기능 플래그 등)
	ProviderStats         mStats;            // 요청/완료/에러 카운터 (mMutex로 보호)
	std::string           mLastError;        // 마지막 에러 메시지 — GetLastError()가 반환
	size_t                mMaxConcurrentOps; // Initialize()에서 설정한 최대 동시 I/O 수
	int64_t               mNextBufferId;     // RegisterBuffer()가 발급하는 단조 증가 버퍼 ID
	std::atomic<uint64_t> mNextOpId{1};      // 각 I/O 작업에 부여하는 고유 키 (relaxed increment)
	std::atomic<bool>     mInitialized;      // 초기화 완료 여부 — acquire/release 사용
	std::atomic<bool>     mShuttingDown{false}; // 종료 진행 플래그 — ProcessCompletions 조기 반환에 사용

	bool LoadRIOFunctions();
	AsyncIOError GetOrCreateRequestQueue(SocketHandle socket, RIO_RQ &outQueue,
									 RequestContext contextForSocket);
	void CleanupPendingOperation(PendingOperation &op);
};

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
