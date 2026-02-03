#pragma once

// English: Client packet handler for TestServer
// 한글: TestServer 클라이언트 패킷 핸들러

#include "Network/Core/PacketDefine.h"
#include "Network/Core/Session.h"

namespace Network::TestServer
{
class TestClientPacketHandler
{
  public:
	// English: Dispatch incoming packets from TestClient
	// 한글: TestClient로부터 받은 패킷을 분기 처리
	void HandlePacket(Core::Session &session, const Core::PacketHeader *header,
					  const char *data, uint32_t size);

  private:
	void HandleConnectRequest(Core::Session &session,
							  const Core::PKT_SessionConnectReq *packet);
	void HandlePingRequest(Core::Session &session,
						   const Core::PKT_PingReq *packet);
};

} // namespace Network::TestServer
