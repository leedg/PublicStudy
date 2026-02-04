#pragma once

// English: Client packet handler for TestServer
// 한글: TestServer용 클라이언트 패킷 핸들러

#include "Network/Core/Session.h"
#include "Network/Core/PacketDefine.h"
#include "Utils/NetworkUtils.h"

namespace Network::TestServer
{
    using Utils::ConnectionId;
    using Core::PacketHeader;
    using Core::PacketType;

    // =============================================================================
    // English: ClientPacketHandler - handles packets from game clients
    // 한글: ClientPacketHandler - 게임 클라이언트의 패킷 처리
    // =============================================================================

    class ClientPacketHandler
    {
    public:
        ClientPacketHandler();
        virtual ~ClientPacketHandler();

        // English: Process incoming packet from client
        // 한글: 클라이언트로부터 받은 패킷 처리
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

    private:
        // English: Individual packet handlers
        // 한글: 개별 패킷 핸들러들
        void HandleConnectRequest(Core::Session* session, const Core::PKT_SessionConnectReq* packet);
        void HandlePingRequest(Core::Session* session, const Core::PKT_PingReq* packet);
    };

} // namespace Network::TestServer
