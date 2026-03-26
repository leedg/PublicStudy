#pragma once

// Windows용 IOCP 기반 AsyncIOProvider 구현.
//
// =============================================================================
// IocpAsyncIOProvider vs WindowsNetworkEngine(IOCP 모드) 역할 구분
// =============================================================================
//
// IocpAsyncIOProvider:
//   - AsyncIOProvider 인터페이스를 위한 저수준 IOCP 추상화.
//   - 플랫폼 독립 설계 — RIO/epoll/io_uring 구현으로 교체 가능.
//   - 세션 생명주기와 독립적으로 I/O 작업을 처리한다.
//
// WindowsNetworkEngine (IOCP 모드):
//   - 세션 관리를 포함한 고수준 서버 엔진.
//   - IocpAsyncIOProvider를 백엔드로 사용하여 accept/recv/send를 조율한다.
//   - Session 생명주기 이벤트(Connected/Disconnected)를 발생시킨다.
//
// IOCP vs RIO 선택 기준:
//   - IOCP: 모든 Windows에서 동작, 호환성 우선, per-op 버퍼 동적 할당.
//   - RIO  : Windows 8+, 사전 등록 슬랩 풀로 WSA 10055(WSAENOBUFS) 방지,
//            커널 non-paged pool 고갈을 원천 차단하므로 고처리량 환경에 적합.
// =============================================================================

#include "Network/Core/AsyncIOProvider.h"

#ifdef _WIN32
#include <atomic>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{


class IocpAsyncIOProvider : public AsyncIOProvider
{
  public:
	// 생성자
	IocpAsyncIOProvider();

	// 소멸자 — IOCP 핸들 및 pending 작업 해제
	virtual ~IocpAsyncIOProvider();

	// 복사 방지 (move-only 의미론)
	IocpAsyncIOProvider(const IocpAsyncIOProvider &) = delete;
	IocpAsyncIOProvider &operator=(const IocpAsyncIOProvider &) = delete;

	// =====================================================================
	// 생명주기 관리
	// =====================================================================

	AsyncIOError Initialize(size_t queueDepth, size_t maxConcurrent) override;
	void Shutdown() override;
	bool IsInitialized() const override;

	// =====================================================================
	// 소켓 연결
	// =====================================================================

	AsyncIOError AssociateSocket(SocketHandle socket,
								RequestContext context) override;

	// =====================================================================
	// 버퍼 관리
	// =====================================================================

	int64_t RegisterBuffer(const void *ptr, size_t size) override;
	AsyncIOError UnregisterBuffer(int64_t bufferId) override;

	// =====================================================================
	// 비동기 I/O 요청
	// =====================================================================

	AsyncIOError SendAsync(SocketHandle socket, const void *buffer, size_t size,
							   RequestContext context, uint32_t flags = 0) override;

	AsyncIOError RecvAsync(SocketHandle socket, void *buffer, size_t size,
							   RequestContext context, uint32_t flags = 0) override;

	AsyncIOError FlushRequests() override;

	// =====================================================================
	// 완료 처리
	// =====================================================================

	int ProcessCompletions(CompletionEntry *entries, size_t maxEntries,
							   int timeoutMs = 0) override;

	// =====================================================================
	// 정보 및 통계
	// =====================================================================

	const ProviderInfo &GetInfo() const override;
	ProviderStats GetStats() const override;
	const char *GetLastError() const override;

  private:
	// =====================================================================
	// 내부 데이터 구조
	// =====================================================================

	// 대기 중인 I/O 작업 추적 구조체
	struct PendingOperation
	{
		OVERLAPPED                 mOverlapped;  // IOCP 오버랩 구조체 — 포인터 캐스트를 위해 반드시 첫 번째 멤버
		WSABUF                     mWsaBuffer;   // WSASend/WSARecv에 전달하는 scatter-gather 버퍼 디스크립터
		std::unique_ptr<uint8_t[]> mBuffer;      // Send: 커널이 pin하는 송신 버퍼; Recv: WSARecv 수신 대상 내부 버퍼
		void*                      mCallerBuffer{nullptr}; // Recv 전용 — ProcessCompletions에서 복사 대상이 되는 호출자 버퍼
		size_t                     mCallerSize{0};         // Recv 전용 — 호출자 버퍼 크기 (복사량 상한)
		RequestContext             mContext;     // ConnectionId — 완료 시 SessionManager 조회에 사용
		AsyncIOType                mType;        // I/O 방향 (Recv 또는 Send)
		SocketHandle               mSocket = INVALID_SOCKET; // 소유 소켓 — OVERLAPPED*에서 O(1) 맵 탐색에 사용
	};

	// =====================================================================
	// 멤버 변수
	// =====================================================================

	// ─────────────────────────────────────────────
	// IOCP 핸들 및 pending 맵
	// ─────────────────────────────────────────────
	HANDLE mCompletionPort; // IOCP 완료 포트 핸들 — CreateIoCompletionPort로 생성 (Windows 전용)

	std::unordered_map<OVERLAPPED *, std::unique_ptr<PendingOperation>>
		mPendingRecvOps; // 대기 중인 recv 작업 맵 (OVERLAPPED* → PendingOperation); mMutex로 보호
	std::unordered_map<OVERLAPPED *, std::unique_ptr<PendingOperation>>
		mPendingSendOps; // 대기 중인 send 작업 맵 (OVERLAPPED* → PendingOperation); mMutex로 보호

	mutable std::mutex mMutex; // mPending* 맵과 mStats 접근을 직렬화하는 뮤텍스

	// ─────────────────────────────────────────────
	// 상태 및 통계
	// ─────────────────────────────────────────────
	ProviderInfo  mInfo;             // 플랫폼 정보 (이름, 지원 기능 플래그 등)
	ProviderStats mStats;            // 요청/완료/에러 카운터 (mMutex로 보호)
	std::string   mLastError;        // 마지막 에러 메시지 — GetLastError()가 반환
	size_t        mMaxConcurrentOps; // Initialize()에서 설정한 최대 동시 I/O 수

	std::atomic<bool> mInitialized;        // 초기화 완료 여부 — acquire/release 사용
	std::atomic<bool> mShuttingDown{false}; // 종료 진행 플래그 — CloseHandle 전에 설정하여 ProcessCompletions 조기 반환
};

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif // _WIN32
