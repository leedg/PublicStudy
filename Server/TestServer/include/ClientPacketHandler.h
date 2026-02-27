#pragma once

// English: Client packet handler for TestServer
// 한글: TestServer용 클라이언트 패킷 핸들러

#include "Network/Core/Session.h"
#include "Network/Core/PacketDefine.h"
#include "Utils/NetworkUtils.h"
#include <functional>
#include <unordered_map>

namespace Network::TestServer
{
    using Utils::ConnectionId;
    using Core::PacketHeader;
    using Core::PacketType;

    // =============================================================================
    // English: ClientPacketHandler - handles packets from game clients using functor array
    // 한글: ClientPacketHandler - 펑터 배열을 사용하여 게임 클라이언트 패킷 처리
    // =============================================================================

    class ClientPacketHandler
    {
    public:
        // English: Packet handler functor type
        // 한글: 패킷 핸들러 펑터 타입
        using PacketHandlerFunc = std::function<void(Core::Session*, const char*, uint32_t)>;

        ClientPacketHandler();
        virtual ~ClientPacketHandler();

        // English: Process incoming packet from client (uses functor dispatch)
        // 한글: 클라이언트로부터 받은 패킷 처리 (펑터 디스패치 사용)
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

    private:
        // English: Packet handler functor map (PacketType -> Handler)
        // 한글: 패킷 핸들러 펑터 맵 (PacketType -> Handler)
        std::unordered_map<uint16_t, PacketHandlerFunc> mHandlers;

        // English: Register all packet handlers
        // 한글: 모든 패킷 핸들러 등록
        void RegisterHandlers();

        // English: Individual packet handlers
        // 한글: 개별 패킷 핸들러들
        void HandleConnectRequest(Core::Session* session, const Core::PKT_SessionConnectReq* packet);
        void HandlePingRequest(Core::Session* session, const Core::PKT_PingReq* packet);
    };

} // namespace Network::TestServer
