#pragma once

// Client packet handler for TestServer

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
    // ClientPacketHandler - handles packets from game clients using functor array
    // =============================================================================

    class ClientPacketHandler
    {
    public:
        // Packet handler functor type
        using PacketHandlerFunc = std::function<void(Core::Session*, const char*, uint32_t)>;

        ClientPacketHandler();
        virtual ~ClientPacketHandler();

        // Process incoming packet from client (uses functor dispatch)
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

    private:
        // Packet handler functor map (PacketType -> Handler)
        std::unordered_map<uint16_t, PacketHandlerFunc> mHandlers;

        // Register all packet handlers
        void RegisterHandlers();

        // Individual packet handlers
        void HandleConnectRequest(Core::Session* session, const Core::PKT_SessionConnectReq* packet);
        void HandlePingRequest(Core::Session* session, const Core::PKT_PingReq* packet);
    };

} // namespace Network::TestServer
