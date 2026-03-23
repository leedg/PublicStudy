#pragma once

// 테스트용 간단한 메시지 핸들러.
// BaseMessageHandler와 달리 독립적으로 동작하며 테스트 시나리오에서 직접 사용한다.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Network::Protocols
{
// =============================================================================
// 타입 정의
// =============================================================================

using ConnectionId = uint64_t;

// =============================================================================
// 메시지 타입
//
// 값 범위:
//   0       : Unknown — 파싱 실패 / 미초기화 sentinel
//   1 ~ 999 : 시스템 예약 (Ping/Pong 등)
//   1000 ~  : CustomStart — 테스트 시나리오별 확장 영역
// =============================================================================

enum class MessageType : uint32_t
{
	Unknown     = 0,
	Ping        = 1,    // Client → Server 생존 확인 요청
	Pong        = 2,    // Server → Client 응답
	CustomStart = 1000
};

// =============================================================================
// 메시지 구조체
// =============================================================================

struct Message
{
	MessageType          mType         = MessageType::Unknown;
	ConnectionId         mConnectionId = 0;
	std::vector<uint8_t> mData;         // 헤더를 제외한 페이로드
	uint64_t             mTimestamp     = 0; // 밀리초 (system_clock 기준)
};

// =============================================================================
// 메시지 핸들러
//
// 스레드 안전성: Register/Unregister/ProcessMessage 모두 mMutex로 보호.
// =============================================================================

using MessageHandlerCallback = std::function<void(const Message &)>;

class MessageHandler
{
  public:
	MessageHandler();
	virtual ~MessageHandler() = default;

	// =====================================================================
	// 핸들러 등록/해제
	// =====================================================================

	/**
	 * 특정 메시지 타입에 대한 콜백을 등록한다.
	 * 같은 타입으로 재등록하면 이전 콜백이 덮어씌워진다.
	 */
	bool RegisterHandler(MessageType type, MessageHandlerCallback callback);

	void UnregisterHandler(MessageType type);

	// =====================================================================
	// 메시지 처리
	// =====================================================================

	/**
	 * 수신 메시지를 파싱하고 등록된 콜백에 디스패치한다.
	 * 헤더 포맷: [type(4)][connectionId(8)][timestamp(8)][payload...]
	 */
	bool ProcessMessage(ConnectionId connectionId, const uint8_t *data,
						size_t size);

	/**
	 * 전송용 메시지를 직렬화한다.
	 * 헤더 포맷: [type(4)][connectionId(8)][timestamp(8)][payload...]
	 */
	std::vector<uint8_t> CreateMessage(MessageType type,
										   ConnectionId connectionId,
										   const void *data, size_t size);

	// =====================================================================
	// 유틸리티
	// =====================================================================

	uint64_t GetCurrentTimestamp() const;

	/**
	 * 원시 데이터의 첫 4바이트에서 메시지 타입을 추출한다.
	 * Unknown/Custom 범위 외의 값은 Unknown으로 반환한다.
	 */
	static MessageType GetMessageType(const uint8_t *data, size_t size);

	static bool ValidateMessage(const uint8_t *data, size_t size);

  private:
	std::unordered_map<MessageType, MessageHandlerCallback> mHandlers;
	uint32_t mNextMessageId;
	std::mutex mMutex;
};

} // namespace Network::Protocols
