// 테스트용 MessageHandler 구현

#include "MessageHandler.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace Network::Protocols
{

MessageHandler::MessageHandler() : mNextMessageId(1) {}

bool MessageHandler::RegisterHandler(MessageType type,
									 MessageHandlerCallback callback)
{
	std::lock_guard<std::mutex> lock(mMutex);

	mHandlers[type] = callback;
	return true;
}

void MessageHandler::UnregisterHandler(MessageType type)
{
	std::lock_guard<std::mutex> lock(mMutex);

	mHandlers.erase(type);
}

bool MessageHandler::ProcessMessage(ConnectionId connectionId,
									const uint8_t *data, size_t size)
{
	if (!data || size == 0)
	{
		return false;
	}

	const size_t headerSize =
		sizeof(uint32_t) + sizeof(ConnectionId) + sizeof(uint64_t);
	if (size < headerSize)
	{
		return false;
	}

	MessageType type = GetMessageType(data, size);
	if (type == MessageType::Unknown)
	{
		return false;
	}

	// 헤더에서 타임스탬프를 파싱하고 페이로드만 콜백에 전달한다.
	uint64_t headerTimestamp = 0;
	std::memcpy(&headerTimestamp,
				data + sizeof(uint32_t) + sizeof(ConnectionId),
				sizeof(headerTimestamp));

	Message message;
	message.mType = type;
	message.mConnectionId = connectionId;
	message.mData.assign(data + headerSize, data + size);
	message.mTimestamp = headerTimestamp;

	std::lock_guard<std::mutex> lock(mMutex);

	auto it = mHandlers.find(type);
	if (it != mHandlers.end())
	{
		it->second(message);
		return true;
	}

	return false;
}

std::vector<uint8_t> MessageHandler::CreateMessage(MessageType type,
													   ConnectionId connectionId,
													   const void *data,
													   size_t size)
{
	std::vector<uint8_t> message;

	// 메시지 포맷: [type(4)][connectionId(8)][timestamp(8)][payload]

	uint32_t typeValue = static_cast<uint32_t>(type);
	message.insert(message.end(), reinterpret_cast<const uint8_t *>(&typeValue),
					   reinterpret_cast<const uint8_t *>(&typeValue) +
						   sizeof(typeValue));

	message.insert(message.end(),
					   reinterpret_cast<const uint8_t *>(&connectionId),
					   reinterpret_cast<const uint8_t *>(&connectionId) +
						   sizeof(connectionId));

	uint64_t timestamp = GetCurrentTimestamp();
	message.insert(message.end(), reinterpret_cast<const uint8_t *>(&timestamp),
					   reinterpret_cast<const uint8_t *>(&timestamp) +
						   sizeof(timestamp));

	if (data && size > 0)
	{
		message.insert(message.end(), static_cast<const uint8_t *>(data),
						   static_cast<const uint8_t *>(data) + size);
	}

	return message;
}

MessageType MessageHandler::GetMessageType(const uint8_t *data, size_t size)
{
	if (!data || size < sizeof(uint32_t))
	{
		return MessageType::Unknown;
	}

	uint32_t typeValue;
	std::memcpy(&typeValue, data, sizeof(uint32_t));

	if (typeValue == static_cast<uint32_t>(MessageType::Ping) ||
		typeValue == static_cast<uint32_t>(MessageType::Pong) ||
		typeValue >= static_cast<uint32_t>(MessageType::CustomStart))
	{
		return static_cast<MessageType>(typeValue);
	}

	return MessageType::Unknown;
}

bool MessageHandler::ValidateMessage(const uint8_t *data, size_t size)
{
	if (!data ||
		size < sizeof(uint32_t) + sizeof(ConnectionId) + sizeof(uint64_t))
	{
		return false;
	}

	return GetMessageType(data, size) != MessageType::Unknown;
}

uint64_t MessageHandler::GetCurrentTimestamp() const
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
		.count();
}

} // namespace Network::Protocols
