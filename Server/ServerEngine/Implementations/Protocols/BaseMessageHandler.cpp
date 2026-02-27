#include "BaseMessageHandler.h"
#include <chrono>
#include <cstring>

namespace Network::Implementations
{

// Message header structure (internal)
#pragma pack(push, 1)
struct MessageHeader
{
	uint32_t type;
	uint64_t connectionId;
	uint64_t timestamp;
	uint32_t dataSize;
};
#pragma pack(pop)

BaseMessageHandler::BaseMessageHandler() : mNextMessageId(1) {}

bool BaseMessageHandler::ProcessMessage(Interfaces::ConnectionId connectionId,
										const uint8_t *data, size_t size)
{
	if (!ValidateMessage(data, size))
	{
		return false;
	}

	Interfaces::Message message;
	if (!ParseMessage(connectionId, data, size, message))
	{
		return false;
	}

	std::lock_guard<std::mutex> lock(mMutex);
	auto it = mHandlers.find(message.type);
	if (it != mHandlers.end())
	{
		it->second(message);
		return true;
	}

	return false;
}

std::vector<uint8_t>
BaseMessageHandler::CreateMessage(Interfaces::MessageType type,
								  Interfaces::ConnectionId connectionId,
								  const void *data, size_t size)
{
	Interfaces::Message message;
	message.type = type;
	message.connectionId = connectionId;
	message.timestamp = GetCurrentTimestamp();

	if (data && size > 0)
	{
		message.data.resize(size);
		std::memcpy(message.data.data(), data, size);
	}

	return SerializeMessage(message);
}

uint64_t BaseMessageHandler::GetCurrentTimestamp() const
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
		.count();
}

bool BaseMessageHandler::ValidateMessage(const uint8_t *data, size_t size) const
{
	if (!data || size < sizeof(MessageHeader))
	{
		return false;
	}

	const MessageHeader *header = reinterpret_cast<const MessageHeader *>(data);
	size_t expectedSize = sizeof(MessageHeader) + header->dataSize;

	return size >= expectedSize;
}

bool BaseMessageHandler::RegisterHandler(Interfaces::MessageType type,
										 MessageCallback callback)
{
	if (!callback)
	{
		return false;
	}

	std::lock_guard<std::mutex> lock(mMutex);
	mHandlers[type] = callback;
	return true;
}

void BaseMessageHandler::UnregisterHandler(Interfaces::MessageType type)
{
	std::lock_guard<std::mutex> lock(mMutex);
	mHandlers.erase(type);
}

bool BaseMessageHandler::ParseMessage(Interfaces::ConnectionId connectionId,
										  const uint8_t *data, size_t size,
										  Interfaces::Message &outMessage)
{
	if (size < sizeof(MessageHeader))
	{
		return false;
	}

	const MessageHeader *header = reinterpret_cast<const MessageHeader *>(data);

	outMessage.type = static_cast<Interfaces::MessageType>(header->type);
	outMessage.connectionId = connectionId;
	outMessage.timestamp = header->timestamp;

	if (header->dataSize > 0)
	{
		const uint8_t *payload = data + sizeof(MessageHeader);
		outMessage.data.assign(payload, payload + header->dataSize);
	}

	return true;
}

std::vector<uint8_t>
BaseMessageHandler::SerializeMessage(const Interfaces::Message &message)
{
	MessageHeader header;
	header.type = static_cast<uint32_t>(message.type);
	header.connectionId = message.connectionId;
	header.timestamp = message.timestamp;
	header.dataSize = static_cast<uint32_t>(message.data.size());

	std::vector<uint8_t> buffer;
	buffer.resize(sizeof(MessageHeader) + message.data.size());

	std::memcpy(buffer.data(), &header, sizeof(MessageHeader));
	if (!message.data.empty())
	{
		std::memcpy(buffer.data() + sizeof(MessageHeader), message.data.data(),
					message.data.size());
	}

	return buffer;
}

Interfaces::MessageType BaseMessageHandler::GetMessageType(const uint8_t *data,
															   size_t size)
{
	if (!data || size < sizeof(uint32_t))
	{
		return Interfaces::MessageType::Unknown;
	}

	uint32_t type = *reinterpret_cast<const uint32_t *>(data);
	return static_cast<Interfaces::MessageType>(type);
}

} // namespace Network::Implementations
