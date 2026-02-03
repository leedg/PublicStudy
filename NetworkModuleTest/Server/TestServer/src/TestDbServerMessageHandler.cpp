// English: DB server message handler implementation
// 한글: DB 서버 메시지 핸들러 구현

#include "../include/TestDbServerMessageHandler.h"
#include <utility>

namespace Network::TestServer
{
TestDbServerMessageHandler::TestDbServerMessageHandler() = default;

void TestDbServerMessageHandler::Initialize()
{
	// 한글: Ping/Pong 메시지 타입을 분리된 핸들러로 등록한다.
	mMessageHandler.RegisterHandler(
		Network::Protocols::MessageType::Ping,
		[this](const Network::Protocols::Message &message)
		{ OnPingMessageReceived(message); });

	mMessageHandler.RegisterHandler(
		Network::Protocols::MessageType::Pong,
		[this](const Network::Protocols::Message &message)
		{ OnPongMessageReceived(message); });
}

void TestDbServerMessageHandler::SetSendCallback(SendCallback callback)
{
	mSendCallback = std::move(callback);
}

void TestDbServerMessageHandler::ProcessIncomingData(
	Network::Protocols::ConnectionId connectionId, const uint8_t *data,
	size_t size)
{
	mMessageHandler.ProcessMessage(connectionId, data, size);
}

void TestDbServerMessageHandler::SendPing(
	Network::Protocols::ConnectionId connectionId, const std::string &message)
{
	const auto pingPayload = mPingPongHandler.CreatePing(message);
	if (pingPayload.empty())
	{
		Utils::Logger::Warn(
			"Failed to build DBServer ping payload - check protobuf build");
		return;
	}

	SendMessage(Network::Protocols::MessageType::Ping, connectionId,
				pingPayload);
}

void TestDbServerMessageHandler::OnPingMessageReceived(
	const Network::Protocols::Message &message)
{
	const auto pongPayload = mPingPongHandler.CreatePong(
		message.mData, "TestServer Pong Response");

	if (pongPayload.empty())
	{
		Utils::Logger::Warn("Invalid DBServer ping message received");
		return;
	}

	// 한글: DBServer Ping에 대한 Pong 응답을 전송한다.
	SendMessage(Network::Protocols::MessageType::Pong, message.mConnectionId,
				pongPayload);
}

void TestDbServerMessageHandler::OnPongMessageReceived(
	const Network::Protocols::Message &message)
{
	if (!mPingPongHandler.ParsePong(message.mData))
	{
		Utils::Logger::Warn("Invalid DBServer pong message received");
		return;
	}

	const auto now = mMessageHandler.GetCurrentTimestamp();
	const auto latencyMs = now - message.mTimestamp;

	Utils::Logger::Debug(
		"DBServer Pong received - Connection: " +
		std::to_string(message.mConnectionId) + ", Latency: " +
		std::to_string(latencyMs) + "ms");
}

void TestDbServerMessageHandler::SendMessage(
	Network::Protocols::MessageType type,
	Network::Protocols::ConnectionId connectionId,
	const std::vector<uint8_t> &payload)
{
	const auto message =
		mMessageHandler.CreateMessage(type, connectionId, payload.data(),
									  payload.size());

	if (!mSendCallback)
	{
		Utils::Logger::Debug(
			"DBServer send callback not set - message queued size: " +
			std::to_string(message.size()));
		return;
	}

	mSendCallback(connectionId, message);
}

} // namespace Network::TestServer
