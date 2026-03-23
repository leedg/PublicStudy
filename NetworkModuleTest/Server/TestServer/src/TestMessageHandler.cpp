#include "../include/TestMessageHandler.h"
#include <chrono>
#include <cstring>

namespace TestServer
{

TestServerMessageHandler::TestServerMessageHandler() {}

void TestServerMessageHandler::InitializeMessageHandlers()
{
	// Register Ping message handler
	RegisterHandler(Network::Interfaces::MessageType::Ping,
					[this](const Network::Interfaces::Message &msg)
					{ OnPingMessageReceived(msg); });

	// Register Pong message handler
	RegisterHandler(Network::Interfaces::MessageType::Pong,
					[this](const Network::Interfaces::Message &msg)
					{ OnPongMessageReceived(msg); });

	// Register custom message handler for application-specific messages
	RegisterHandler(
		static_cast<Network::Interfaces::MessageType>(static_cast<uint32_t>(
			Network::Interfaces::MessageType::CustomStart)),
		[this](const Network::Interfaces::Message &msg)
		{ OnCustomMessageReceived(msg); });

	std::cout << "[TestServerMessageHandler] Message handlers initialized "
				 "successfully"
				  << std::endl;
}

void TestServerMessageHandler::OnPingMessageReceived(
	const Network::Interfaces::Message &message)
{
	std::cout << "[TestServerMessageHandler] PING received from connection "
				  << message.connectionId << std::endl;

	// Create and prepare PONG response message
	// In a full implementation, this would be sent via the network engine
	auto pongResponseMessage =
		CreateMessage(Network::Interfaces::MessageType::Pong,
						  message.connectionId, nullptr, 0);

	std::cout
		<< "[TestServerMessageHandler] PONG response prepared for connection "
		<< message.connectionId << std::endl;
}

void TestServerMessageHandler::OnPongMessageReceived(
	const Network::Interfaces::Message &message)
{
	// Calculate round-trip latency
	auto currentTimestamp = GetCurrentTimestamp();
	uint64_t roundTripLatencyMs = currentTimestamp - message.timestamp;

	std::cout << "[TestServerMessageHandler] PONG received from connection "
				  << message.connectionId
				  << " (round-trip latency: " << roundTripLatencyMs << "ms)"
				  << std::endl;
}

void TestServerMessageHandler::OnCustomMessageReceived(
	const Network::Interfaces::Message &message)
{
	// 페이로드 첫 4바이트에서 서브타입을 파싱.
	//   클라이언트는 모든 커스텀 메시지 앞에 uint32_t 서브타입을 포함시켜
	//   서버가 아래 핸들러로 디스패치할 수 있도록 한다.
	uint32_t subType = 0;
	if (message.data.size() >= sizeof(uint32_t))
	{
		std::memcpy(&subType, message.data.data(), sizeof(uint32_t));
	}

	const size_t payloadSize = message.data.size();

	std::cout
		<< "[TestServerMessageHandler] Custom message received from connection "
		<< message.connectionId
		<< " (subType=" << subType
		<< ", payloadSize=" << payloadSize << " bytes)"
		<< std::endl;

	// 서브타입에 따라 디스패치.
	// 프로토콜에 새 메시지 타입이 추가될 때 이 switch를 확장한다.
	switch (subType)
	{
	case 0: // 에코 요청 — 동일 서브타입과 페이로드를 그대로 응답으로 반환
	{
		const void* echoPayload = payloadSize > 0 ? message.data.data() : nullptr;
		auto echoMessage = CreateMessage(
			static_cast<Network::Interfaces::MessageType>(
				static_cast<uint32_t>(Network::Interfaces::MessageType::CustomStart)),
			message.connectionId,
			echoPayload,
			payloadSize);

		std::cout
			<< "[TestServerMessageHandler] Echo response prepared for connection "
			<< message.connectionId
			<< " (" << echoMessage.size() << " bytes)"
			<< std::endl;
		break;
	}

	case 1: // 채팅 메시지 — 4바이트 서브타입 접두사 뒤의 UTF-8 텍스트를 추출하여 로깅
	{
		std::string chatText;
		if (payloadSize > sizeof(uint32_t))
		{
			chatText.assign(
				reinterpret_cast<const char*>(message.data.data() + sizeof(uint32_t)),
				payloadSize - sizeof(uint32_t));
		}

		std::cout
			<< "[TestServerMessageHandler] Chat from connection "
			<< message.connectionId << ": \"" << chatText << "\""
			<< std::endl;
		break;
	}

	default: // Unknown subtype — log and discard
	{
		std::cout
			<< "[TestServerMessageHandler] Unknown custom subType=" << subType
			<< " from connection " << message.connectionId
			<< " — discarding"
			<< std::endl;
		break;
	}
	}
}

} // namespace TestServer
