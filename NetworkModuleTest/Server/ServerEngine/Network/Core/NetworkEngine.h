#pragma once

// NetworkModule용 핵심 네트워크 추상화 레이어

#include "../../Utils/NetworkUtils.h"
#include "AsyncIOProvider.h"
#include <functional>
#include <memory>

namespace Network::Core
{
using Utils::ConnectionId;
using Utils::Timestamp;
// =============================================================================
// 네트워크 이벤트 타입
// =============================================================================

enum class NetworkEvent : uint8_t
{
	Connected,    // 새 연결 수립
	Disconnected, // 연결 종료
	DataReceived, // 원시 TCP 세그먼트 수신 (완성 패킷 아님 — 패킷 단위 처리는 SetOnRecv 사용)
	DataSent,     // 데이터 전송 성공
	Error         // 에러 발생
};

// =============================================================================
// 네트워크 이벤트 데이터
// =============================================================================

struct NetworkEventData
{
	NetworkEvent eventType;
	ConnectionId connectionId;
	size_t dataSize;
	OSError errorCode;
	Timestamp timestamp;
	std::unique_ptr<uint8_t[]> data;
};

// =============================================================================
// 이벤트 콜백 타입
// =============================================================================

using NetworkEventCallback = std::function<void(const NetworkEventData &)>;

// =============================================================================
// 핵심 네트워크 인터페이스
// =============================================================================

class INetworkEngine
{
  public:
	virtual ~INetworkEngine() = default;

	// =====================================================================
	// 생명주기 관리
	// =====================================================================

	/**
	 * 네트워크 엔진 초기화
	 * @param maxConnections 최대 허용 연결 수
	 * @param port 수신 대기 포트 번호
	 */
	virtual bool Initialize(size_t maxConnections, uint16_t port) = 0;

	/** 네트워크 엔진 시작 */
	virtual bool Start() = 0;

	/** 네트워크 엔진 중지 */
	virtual void Stop() = 0;

	/** 엔진 실행 상태 확인 */
	virtual bool IsRunning() const = 0;

	// =====================================================================
	// 이벤트 처리
	// =====================================================================

	/**
	 * 이벤트 콜백 등록.
	 * 이벤트 타입 하나당 하나의 콜백만 허용된다 (덮어쓰기 방식).
	 */
	virtual bool RegisterEventCallback(NetworkEvent eventType,
										   NetworkEventCallback callback) = 0;

	/** 이벤트 콜백 등록 해제 */
	virtual void UnregisterEventCallback(NetworkEvent eventType) = 0;

	// =====================================================================
	// 연결 관리
	// =====================================================================

	/** 특정 연결로 데이터 전송 */
	virtual bool SendData(ConnectionId connectionId, const void *data,
						  size_t size) = 0;

	/** 특정 연결 종료 */
	virtual void CloseConnection(ConnectionId connectionId) = 0;

	/** 연결 정보 조회. 연결이 없으면 빈 문자열 반환. */
	virtual std::string GetConnectionInfo(ConnectionId connectionId) const = 0;

	// =====================================================================
	// 통계
	// =====================================================================

	struct Statistics
	{
		uint64_t totalConnections;
		uint64_t activeConnections;
		uint64_t totalBytesSent;
		uint64_t totalBytesReceived;
		// 방향별 에러 카운터. totalErrors == sendErrors + recvErrors.
		uint64_t totalSendErrors;
		uint64_t totalRecvErrors;
		uint64_t totalErrors;
		Timestamp startTime;
	};

	/** 엔진 통계 조회 */
	virtual Statistics GetStatistics() const = 0;
};

// =============================================================================
// 팩토리 함수
// =============================================================================

/**
 * 네트워크 엔진 인스턴스 생성.
 * @param engineType 엔진 타입 ("auto", "iocp", "rio", "epoll", "io_uring", "kqueue")
 * @return 네트워크 엔진 인스턴스, 지원하지 않으면 nullptr
 */
std::unique_ptr<INetworkEngine>
CreateNetworkEngine(const std::string &engineType = "auto");

/** 현재 플랫폼에서 사용 가능한 엔진 타입 목록 조회 */
std::vector<std::string> GetAvailableEngineTypes();

} // namespace Network::Core
