#pragma once

// English: Simple message handler for network messages
// 한글: 네트워크 메시지용 간단한 메시지 핸들러

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Network::Protocols
{
// =============================================================================
// English: Type definitions
// 한글: 타입 정의
// =============================================================================

using ConnectionId = uint64_t;

// =============================================================================
// English: Message types
// 한글: 메시지 타입
// =============================================================================

enum class MessageType : uint32_t
{
	// English: Unknown or invalid message
	// 한글: 알 수 없거나 유효하지 않은 메시지
	Unknown = 0,

	// English: Ping message
	// 한글: Ping 메시지
	Ping = 1,

	// English: Pong response
	// 한글: Pong 응답
	Pong = 2,

	// English: Custom message start
	// 한글: 커스텀 메시지 시작
	CustomStart = 1000
};

// =============================================================================
// English: Message structure
// 한글: 메시지 구조체
// =============================================================================

struct Message
{
	// English: Message type
	// 한글: 메시지 타입
	MessageType mType = MessageType::Unknown;

	// English: Connection ID that sent this message
	// 한글: 이 메시지를 보낸 연결 ID
	ConnectionId mConnectionId = 0;

	// English: Message payload (header excluded)
	// 한글: 메시지 페이로드 (헤더 제외)
	std::vector<uint8_t> mData;

	// English: Timestamp from message header
	// 한글: 메시지 헤더에 포함된 타임스탬프
	uint64_t mTimestamp = 0;
};

// =============================================================================
// English: Message handler interface
// 한글: 메시지 핸들러 인터페이스
// =============================================================================

using MessageHandlerCallback = std::function<void(const Message &)>;

class MessageHandler
{
  public:
	// English: Constructor
	// 한글: 생성자
	MessageHandler();

	// English: Destructor
	// 한글: 소멸자
	virtual ~MessageHandler() = default;

	// =====================================================================
	// English: Registration
	// 한글: 등록
	// =====================================================================

	/**
	 * English: Register a callback for specific message type
	 * 한글: 특정 메시지 타입용 콜백 등록
	 * @param type Message type
	 * @param callback Handler function
	 * @return True if registration successful
	 */
	bool RegisterHandler(MessageType type, MessageHandlerCallback callback);

	/**
	 * English: Unregister a handler
	 * 한글: 핸들러 등록 해제
	 * @param type Message type
	 */
	void UnregisterHandler(MessageType type);

	// =====================================================================
	// English: Message processing
	// 한글: 메시지 처리
	// =====================================================================

	/**
	 * English: Process incoming message data
	 * 한글: 수신 메시지 데이터 처리
	 * @param connectionId Source connection ID
	 * @param data Raw message data
	 * @param size Data size
	 * @return True if message was processed successfully
	 */
	bool ProcessMessage(ConnectionId connectionId, const uint8_t *data,
						size_t size);

	/**
	 * English: Create message for sending
	 * 한글: 전송용 메시지 생성
	 * @param type Message type
	 * @param connectionId Target connection ID
	 * @param data Message payload
	 * @param size Payload size
	 * @return Serialized message ready for network send
	 */
	std::vector<uint8_t> CreateMessage(MessageType type,
										   ConnectionId connectionId,
										   const void *data, size_t size);

	// =====================================================================
	// English: Utility
	// 한글: 유틸리티
	// =====================================================================

	/**
	 * English: Get current timestamp in milliseconds
	 * 한글: 현재 타임스탬프(밀리초) 조회
	 * @return Current timestamp
	 */
	uint64_t GetCurrentTimestamp() const;

	/**
	 * English: Get message type from raw data
	 * 한글: 원본 데이터로부터 메시지 타입 조회
	 * @param data Raw message data
	 * @param size Data size
	 * @return Message type (Unknown if invalid)
	 */
	static MessageType GetMessageType(const uint8_t *data, size_t size);

	/**
	 * English: Validate message format
	 * 한글: 메시지 형식 검증
	 * @param data Raw message data
	 * @param size Data size
	 * @return True if message format is valid
	 */
	static bool ValidateMessage(const uint8_t *data, size_t size);

  private:
	// English: Registered handlers
	// 한글: 등록된 핸들러들
	std::unordered_map<MessageType, MessageHandlerCallback> mHandlers;

	// English: Next message ID
	// 한글: 다음 메시지 ID
	uint32_t mNextMessageId;

	// English: Thread safety
	// 한글: 스레드 안전성
	std::mutex mMutex;
};

} // namespace Network::Protocols
