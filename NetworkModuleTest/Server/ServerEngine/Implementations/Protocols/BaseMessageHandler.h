#pragma once

#include "../../Interfaces/IMessageHandler.h"
#include "../../Interfaces/Message.h"
#include "../../Interfaces/MessageType_enum.h"
#include <functional>
#include <mutex>
#include <unordered_map>

namespace Network::Implementations
{

/**
 * Base implementation of IMessageHandler
 * Provides common functionality for message handling
 * Derived classes should implement specific message processing logic
 */
class BaseMessageHandler : public Interfaces::IMessageHandler
{
  public:
	using MessageCallback = std::function<void(const Interfaces::Message &)>;

	BaseMessageHandler();
	virtual ~BaseMessageHandler() = default;

	// IMessageHandler implementation
	bool ProcessMessage(Interfaces::ConnectionId connectionId,
						const uint8_t *data, size_t size) override;

	std::vector<uint8_t> CreateMessage(Interfaces::MessageType type,
										   Interfaces::ConnectionId connectionId,
										   const void *data, size_t size) override;

	uint64_t GetCurrentTimestamp() const override;

	bool ValidateMessage(const uint8_t *data, size_t size) const override;

	// Registration API
	/**
	 * Register a callback for specific message type
	 * @param type Message type
	 * @param callback Handler function
	 * @return true if registration successful
	 */
	bool RegisterHandler(Interfaces::MessageType type,
						 MessageCallback callback);

	/**
	 * Unregister a handler
	 * @param type Message type
	 */
	void UnregisterHandler(Interfaces::MessageType type);

  protected:
	/**
	 * Parse message from raw data
	 * @param connectionId Source connection
	 * @param data Raw data
	 * @param size Data size
	 * @param outMessage Parsed message output
	 * @return true if parsing successful
	 */
	virtual bool ParseMessage(Interfaces::ConnectionId connectionId,
								  const uint8_t *data, size_t size,
								  Interfaces::Message &outMessage);

	/**
	 * Serialize message to raw data
	 * @param message Message to serialize
	 * @return Serialized data
	 */
	virtual std::vector<uint8_t>
	SerializeMessage(const Interfaces::Message &message);

	/**
	 * Get message type from raw data
	 * @param data Raw data
	 * @param size Data size
	 * @return Message type
	 */
	static Interfaces::MessageType GetMessageType(const uint8_t *data,
												  size_t size);

  private:
	// Registered message handlers
	std::unordered_map<Interfaces::MessageType, MessageCallback> mHandlers;

	// Thread safety
	mutable std::mutex mMutex;

	// Next message ID for tracking
	uint32_t mNextMessageId;
};

} // namespace Network::Implementations
