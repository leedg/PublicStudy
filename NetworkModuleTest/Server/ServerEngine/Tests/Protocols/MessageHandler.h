#pragma once

// Simple message handler for network messages

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Network::Protocols
{
// =============================================================================
// Type definitions
// =============================================================================

using ConnectionId = uint64_t;

// =============================================================================
// Message types
// =============================================================================

enum class MessageType : uint32_t
{
	// Unknown or invalid message
	Unknown = 0,

	// Ping message
	Ping = 1,

	// Pong response
	Pong = 2,

	// Custom message start
	CustomStart = 1000
};

// =============================================================================
// Message structure
// =============================================================================

struct Message
{
	// Message type
	MessageType mType = MessageType::Unknown;

	// Connection ID that sent this message
	ConnectionId mConnectionId = 0;

	// Message payload (header excluded)
	std::vector<uint8_t> mData;

	// Timestamp from message header
	uint64_t mTimestamp = 0;
};

// =============================================================================
// Message handler interface
// =============================================================================

using MessageHandlerCallback = std::function<void(const Message &)>;

class MessageHandler
{
  public:
	// Constructor
	MessageHandler();

	// Destructor
	virtual ~MessageHandler() = default;

	// =====================================================================
	// Registration
	// =====================================================================

	/**
	 * Register a callback for specific message type
	 * @param type Message type
	 * @param callback Handler function
	 * @return True if registration successful
	 */
	bool RegisterHandler(MessageType type, MessageHandlerCallback callback);

	/**
	 * Unregister a handler
	 * @param type Message type
	 */
	void UnregisterHandler(MessageType type);

	// =====================================================================
	// Message processing
	// =====================================================================

	/**
	 * Process incoming message data
	 * @param connectionId Source connection ID
	 * @param data Raw message data
	 * @param size Data size
	 * @return True if message was processed successfully
	 */
	bool ProcessMessage(ConnectionId connectionId, const uint8_t *data,
						size_t size);

	/**
	 * Create message for sending
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
	// Utility
	// =====================================================================

	/**
	 * Get current timestamp in milliseconds
	 * @return Current timestamp
	 */
	uint64_t GetCurrentTimestamp() const;

	/**
	 * Get message type from raw data
	 * @param data Raw message data
	 * @param size Data size
	 * @return Message type (Unknown if invalid)
	 */
	static MessageType GetMessageType(const uint8_t *data, size_t size);

	/**
	 * Validate message format
	 * @param data Raw message data
	 * @param size Data size
	 * @return True if message format is valid
	 */
	static bool ValidateMessage(const uint8_t *data, size_t size);

  private:
	// Registered handlers
	std::unordered_map<MessageType, MessageHandlerCallback> mHandlers;

	// Next message ID
	uint32_t mNextMessageId;

	// Thread safety
	std::mutex mMutex;
};

} // namespace Network::Protocols
