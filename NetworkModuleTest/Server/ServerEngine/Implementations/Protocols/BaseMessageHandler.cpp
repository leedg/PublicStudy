#include "BaseMessageHandler.h"
#include <chrono>
#include <cstring>

namespace Network::Implementations
{

// 내부 메시지 헤더 레이아웃 (packed — 패딩 없음):
//   [type: uint32_t][connectionId: uint64_t][timestamp: uint64_t][dataSize: uint32_t]
#pragma pack(push, 1)
struct MessageHeader
{
	uint32_t type;         // MessageType 열거값 (uint32_t 캐스트)
	uint64_t connectionId; // 송신 또는 수신 측 연결 ID
	uint64_t timestamp;    // 메시지 생성 시각 (system_clock 밀리초)
	uint32_t dataSize;     // 헤더 이후 페이로드 크기 (바이트)
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
