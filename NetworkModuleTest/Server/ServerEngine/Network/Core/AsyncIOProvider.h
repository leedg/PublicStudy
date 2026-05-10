#pragma once
// encoding: UTF-8

// 모든 플랫폼의 비동기 I/O를 통일하는 인터페이스
//
// =============================================================================
// 설계 철학
// =============================================================================
//
// 크로스 플랫폼 비동기 I/O를 위한 저수준 추상화.
// 다음 경우에 사용:
//   - 멀티플랫폼 네트워크 라이브러리 구축
//   - Session과 독립적인 I/O 작업 필요
//   - IOCP/RIO/epoll/io_uring 간 동적 전환 필요
//
// 다음 경우에는 사용하지 않을 것:
//   - Windows 전용 서버 구축 (IOCPNetworkEngine 직접 사용)
//   - Session 생명주기 관리 필요 (IOCPNetworkEngine + Session 사용)
// =============================================================================

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
// winsock2는 windows.h보다 먼저 포함해야 한다 (windows.h가 winsock.h를 먼저 당기면 충돌).
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
// Windows 소켓 핸들 타입
using SocketHandle = SOCKET;
// Windows OS 에러 타입 (GetLastError/WSAGetLastError 반환값)
using OSError = DWORD;
#else
#include <sys/socket.h>
// POSIX 소켓 핸들 타입 (파일 디스크립터)
using SocketHandle = int;
// POSIX OS 에러 타입 (errno)
using OSError = int;
#endif

namespace Network
{
namespace AsyncIO
{
// =============================================================================
// 타입 정의
// =============================================================================

// 비동기 작업용 사용자 정의 컨텍스트 (주로 ConnectionId를 담는다)
using RequestContext = uint64_t;

// 완료 콜백 함수 타입
using CompletionCallback =
	std::function<void(const struct CompletionEntry &, void *userData)>;

// =============================================================================
// 열거형
// =============================================================================

// 비동기 I/O 작업 타입
enum class AsyncIOType : uint8_t
{
	Send,    // 송신 작업
	Recv,    // 수신 작업
	Accept,  // 연결 수락 (리스너)
	Connect, // 연결 요청 (클라이언트)
	Timeout, // 타임아웃 (내부 사용)
	Error,   // 에러 (내부 사용)
};

// 플랫폼 타입 (AsyncIO 백엔드 구현을 나타내며, OS 플랫폼 자체가 아님)
// - Windows: 기본 = IOCP, 고성능 = RIO
// - Linux  : 기본 = epoll, 고성능 = IOUring
// - macOS  : kqueue
enum class PlatformType : uint8_t
{
	IOCP,    // Windows IOCP (안정적, 모든 Windows 버전)
	RIO,     // Windows Registered I/O (고성능, Windows 8+ 전용)
	Epoll,   // Linux epoll (안정적, 모든 Linux)
	IOUring, // Linux io_uring (고성능, 커널 5.1+)
	Kqueue,  // macOS kqueue (표준)
};

// 비동기 I/O 작업의 에러 코드
// 음수값을 사용하여 성공(0)과 명확히 구분한다.
enum class AsyncIOError : int32_t
{
	Success             =  0,  // 작업 성공
	NotInitialized      = -1,  // 공급자가 초기화되지 않음
	InvalidSocket       = -2,  // 잘못된 소켓 핸들
	OperationPending    = -3,  // 작업이 비동기로 대기 중 (에러 아님)
	OperationFailed     = -4,  // OS 수준 작업 실패
	InvalidBuffer       = -5,  // 잘못된 버퍼 포인터 또는 크기
	NoResources         = -6,  // 사용 가능한 리소스 없음
	Timeout             = -7,  // 작업 타임아웃
	PlatformNotSupported= -8,  // 현재 플랫폼에서 지원하지 않음
	AlreadyInitialized  = -9,  // 이미 초기화됨
	InvalidParameter    = -10, // 잘못된 매개변수
	AllocationFailed    = -11, // 메모리 할당 실패
	ResourceExhausted   = -12, // 풀/큐 등 리소스 고갈
};

// 버퍼 등록 정책
enum class BufferPolicy : uint8_t
{
	Reuse,     // 버퍼를 여러 작업에 재사용 가능
	SingleUse, // 버퍼를 한 번 사용 후 해제
	Pooled,    // 버퍼가 풀에서 제공됨
};

// =============================================================================
// 구조체
// =============================================================================

// I/O 완료 항목
struct CompletionEntry
{
	RequestContext mContext;       // 요청 컨텍스트 (주로 ConnectionId)
	AsyncIOType    mType;          // 작업 타입 (Send/Recv 등)
	int32_t        mResult;        // 전송된 바이트 수. 0 이하면 연결 종료 또는 에러.
	OSError        mOsError;       // OS 에러 코드 (0 = 성공)
	uint64_t       mCompletionTime;// 완료 시간 (나노초, 선택사항 — 0이면 미측정)
};

// 송수신 버퍼 구조체
struct IOBuffer
{
	void  *mData;   // 버퍼 포인터
	size_t mSize;   // 버퍼 크기
	size_t mOffset; // 오프셋 (RIO BufferId 대신 사용 가능)
};

// 공급자 정보 구조체
struct ProviderInfo
{
	PlatformType mPlatformType;    // 플랫폼 타입 (백엔드 구현)
	const char  *mName;            // 사람이 읽을 수 있는 이름 ("IOCP", "RIO", "io_uring" 등)
	uint32_t     mCapabilities;    // 기능 플래그 (지원 기능 비트마스크)
	size_t       mMaxQueueDepth;   // 최대 큐 깊이
	size_t       mMaxConcurrentReq;// 최대 동시 요청 수
	bool         mSupportsBufferReg; // 버퍼 사전 등록 지원 (RIO/io_uring)
	bool         mSupportsBatching;  // 배치 처리 지원
	bool         mSupportsZeroCopy;  // Zero-copy 지원
};

// 공급자 통계 구조체
struct ProviderStats
{
	uint64_t mTotalRequests;   // 전체 요청 수
	uint64_t mTotalCompletions;// 전체 완료 수
	uint64_t mPendingRequests; // 현재 대기 중인 요청 수
	uint64_t mAvgLatency;      // 평균 레이턴시 (나노초)
	double   mP99Latency;      // P99 레이턴시 (나노초)
	uint64_t mErrorCount;      // 에러 수
};

// 플랫폼 정보 (런타임 감지용)
struct PlatformInfo
{
	PlatformType mPlatformType;  // 감지된 플랫폼 타입
	uint32_t     mMajorVersion;  // OS 주 버전
	uint32_t     mMinorVersion;  // OS 부 버전
	const char  *mPlatformName;  // 플랫폼 이름 문자열
	bool         mSupportRIO;    // Windows RIO 지원 여부
	bool         mSupportIOUring;// Linux io_uring 지원 여부
	bool         mSupportKqueue; // macOS kqueue 지원 여부
};

// 버퍼 등록 결과
struct BufferRegistration
{
	int64_t mBufferId;  // 버퍼 ID (향후 UnregisterBuffer에 전달)
	bool    mSuccess;   // 등록 성공 여부
	int32_t mErrorCode; // 실패 시 에러 코드 (성공 시 0)
};

// =============================================================================
// 추상 인터페이스: AsyncIOProvider
// =============================================================================

class AsyncIOProvider
{
  public:
	virtual ~AsyncIOProvider() = default;

	// =====================================================================
	// 생명주기 관리
	// =====================================================================

	/**
	 * 비동기 I/O 공급자 초기화
	 * @param queueDepth 요청/완료 큐 깊이 (권장 범위: 32–4096)
	 * @param maxConcurrent 최대 동시 요청 수
	 */
	virtual AsyncIOError Initialize(size_t queueDepth,
									size_t maxConcurrent) = 0;

	/** 비동기 I/O 공급자 종료 */
	virtual void Shutdown() = 0;

	/** 공급자 초기화 여부 확인 */
	virtual bool IsInitialized() const = 0;

	// =====================================================================
	// 버퍼 관리
	// =====================================================================

	/**
	 * 최적화된 I/O용 버퍼 사전 등록 (RIO/io_uring 전용).
	 * IOCP는 no-op으로 처리한다.
	 * @return 버퍼 ID (>= 0이면 성공, < 0이면 에러)
	 */
	virtual int64_t RegisterBuffer(const void *ptr, size_t size) = 0;

	/**
	 * 이전에 등록된 버퍼 등록 해제
	 * @param bufferId RegisterBuffer()의 반환값
	 */
	virtual AsyncIOError UnregisterBuffer(int64_t bufferId) = 0;

	// =====================================================================
	// 소켓 연결
	// =====================================================================

	/**
	 * accept() 후 소켓을 I/O 공급자에 등록. 비동기 I/O 전에 반드시 호출해야 한다.
	 *
	 * 플랫폼별 동작:
	 * - IOCP: CreateIoCompletionPort(socket, completionPort, context, 0)
	 * - epoll: epoll_ctl(EPOLL_CTL_ADD)
	 * - kqueue: kevent() with EV_ADD
	 * - RIO/io_uring: 플랫폼별 등록
	 *
	 * @param context 요청 컨텍스트 (주로 ConnectionId)
	 */
	virtual AsyncIOError AssociateSocket(SocketHandle socket,
										 RequestContext context) = 0;

	// =====================================================================
	// 비동기 I/O 요청
	// =====================================================================

	/**
	 * 비동기 송신 작업.
	 * 플랫폼별 동작:
	 * - IOCP: 즉시 실행 (flags 무시)
	 * - RIO: RIO_MSG_DEFER로 배치 처리 대기
	 * - io_uring: 자동 배치 처리
	 */
	virtual AsyncIOError SendAsync(SocketHandle socket, const void *buffer,
									   size_t size, RequestContext context,
									   uint32_t flags = 0) = 0;

	/** 비동기 수신 작업 */
	virtual AsyncIOError RecvAsync(SocketHandle socket, void *buffer,
									   size_t size, RequestContext context,
									   uint32_t flags = 0) = 0;

	/**
	 * 대기 중인 요청 일괄 실행 (배치 처리).
	 * - IOCP: no-op
	 * - RIO: 지연된 송수신을 커널에 커밋
	 * - io_uring: SQ 항목 전체 제출
	 */
	virtual AsyncIOError FlushRequests() = 0;

	// =====================================================================
	// 완료 처리
	// =====================================================================

	/**
	 * 완료된 작업 처리 (논블로킹 또는 타임아웃).
	 * @param timeoutMs 0 = 즉시 반환, >0 = ms 대기, -1 = 무한 대기
	 * @return 처리된 완료 수 (음수 = 에러)
	 */
	virtual int ProcessCompletions(CompletionEntry *entries, size_t maxEntries,
									   int timeoutMs = 0) = 0;

	// =====================================================================
	// 정보 및 통계
	// =====================================================================

	/** 공급자 정보 조회 */
	virtual const ProviderInfo &GetInfo() const = 0;

	/** 공급자 통계 조회 */
	virtual ProviderStats GetStats() const = 0;

	/** 마지막 에러 메시지 조회 */
	virtual const char *GetLastError() const = 0;
};

// =============================================================================
// 팩토리 함수
// =============================================================================

/**
 * 플랫폼 자동 선택으로 AsyncIOProvider 생성.
 * 폴백 체인:
 * - Windows 8+: RIO -> IOCP -> nullptr
 * - Windows 7-: IOCP -> nullptr
 * - Linux 5.1+: io_uring -> epoll -> nullptr
 * - Linux 4.x: epoll -> nullptr
 * - macOS: kqueue -> nullptr
 */
std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider();

/**
 * 명시적 플랫폼 힌트로 AsyncIOProvider 생성.
 * @param platformHint 플랫폼 이름 ("IOCP", "RIO", "io_uring", "epoll" 등)
 * @return 해당 공급자, 지원하지 않으면 nullptr
 */
std::unique_ptr<AsyncIOProvider>
CreateAsyncIOProvider(const char *platformHint);

/** 특정 플랫폼 지원 여부 확인 */
bool IsPlatformSupported(const char *platformHint);

/**
 * 지원하는 모든 플랫폼 이름 목록 반환.
 * @param outCount 출력: 항목 수
 */
const char **GetSupportedPlatforms(size_t &outCount);

/** 런타임에 현재 플랫폼 타입 조회 */
PlatformType GetCurrentPlatform();

/** 상세 플랫폼 정보 조회 */
PlatformInfo GetPlatformInfo();

} // namespace AsyncIO
} // namespace Network
