#pragma once

// TestServer용 클라이언트 패킷 핸들러

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
    // ClientPacketHandler - 펑터 맵을 사용하여 게임 클라이언트 패킷 처리
    // =============================================================================

    class ClientPacketHandler
    {
    public:
        // 패킷 핸들러 펑터 타입
        using PacketHandlerFunc = std::function<void(Core::Session*, const char*, uint32_t)>;

        ClientPacketHandler();
        virtual ~ClientPacketHandler();

        // 클라이언트로부터 받은 패킷 처리 (펑터 맵 디스패치)
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

    private:
        // 패킷 핸들러 펑터 맵 (PacketType → Handler)
        std::unordered_map<uint16_t, PacketHandlerFunc> mHandlers;

        // 생성자에서 모든 패킷 핸들러를 등록
        void RegisterHandlers();

        // 개별 패킷 핸들러
        void HandleConnectRequest(Core::Session* session, const Core::PKT_SessionConnectReq* packet);
        void HandlePingRequest(Core::Session* session, const Core::PKT_PingReq* packet);
    };

} // namespace Network::TestServer
