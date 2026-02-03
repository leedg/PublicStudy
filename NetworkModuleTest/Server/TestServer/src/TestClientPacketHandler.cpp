// English: Client packet handler implementation
// 한글: 클라이언트 패킷 핸들러 구현

#include "../include/TestClientPacketHandler.h"
#include "Utils/NetworkUtils.h"
#include <ctime>

namespace Network::TestServer
{
void TestClientPacketHandler::HandlePacket(Core::Session &session,
										   const Core::PacketHeader *header,
										   const char *data, uint32_t size)
{
	(void)size;
	const auto packetType = static_cast<Core::PacketType>(header->id);

	switch (packetType)
	{
	case Core::PacketType::SessionConnectReq:
		HandleConnectRequest(
			session, reinterpret_cast<const Core::PKT_SessionConnectReq *>(data));
		break;

	case Core::PacketType::PingReq:
		HandlePingRequest(session,
						  reinterpret_cast<const Core::PKT_PingReq *>(data));
		break;

	default:
		Utils::Logger::Warn("Unknown packet type: " +
							std::to_string(header->id));
		break;
	}
}

void TestClientPacketHandler::HandleConnectRequest(
	Core::Session &session, const Core::PKT_SessionConnectReq *packet)
{
	Utils::Logger::Info(
		"Connect request - Session: " + std::to_string(session.GetId()) +
		", ClientVersion: " + std::to_string(packet->clientVersion));

	// 한글: 연결 승인 응답 패킷 생성 및 전송
	Core::PKT_SessionConnectRes response;
	response.sessionId = session.GetId();
	response.serverTime = static_cast<uint32_t>(std::time(nullptr));
	response.result = static_cast<uint8_t>(Core::ConnectResult::Success);

	session.Send(response);
}

void TestClientPacketHandler::HandlePingRequest(Core::Session &session,
												const Core::PKT_PingReq *packet)
{
	session.SetLastPingTime(Utils::Timer::GetCurrentTimestamp());

	// 한글: Ping에 대한 Pong 응답 전송
	Core::PKT_PongRes response;
	response.clientTime = packet->clientTime;
	response.serverTime = Utils::Timer::GetCurrentTimestamp();
	response.sequence = packet->sequence;

	session.Send(response);

	Utils::Logger::Debug("Ping/Pong - Session: " +
						 std::to_string(session.GetId()) +
						 ", Seq: " + std::to_string(packet->sequence));
}

} // namespace Network::TestServer
