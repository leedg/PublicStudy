#pragma once

// DB Server packet handler for TestServer

#include "Network/Core/Session.h"
#include "Network/Core/ServerPacketDefine.h"
#include "Utils/NetworkUtils.h"
#include <atomic>
#include <functional>
#include <unordered_map>
#include <memory>

namespace Network::TestServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // DBServerPacketHandler - handles packets from/to DB server using functor array
    // =============================================================================

    class DBServerPacketHandler
    {
    public:
        // Packet handler functor type
        using PacketHandlerFunc = std::function<void(Core::Session*, const char*, uint32_t)>;

        DBServerPacketHandler();
        virtual ~DBServerPacketHandler();

        // Process incoming packet from DB server (uses functor dispatch)
        void ProcessPacket(Core::Session* session, const char* data, uint32_t size);

        // Send ping to DB server
        void SendPingToDBServer(Core::Session* session);

        // Request saving ping time to database
        void RequestSavePingTime(Core::Session* session, uint32_t serverId, const char* serverName);

    private:
        // Packet handler functor map (ServerPacketType -> Handler)
        std::unordered_map<uint16_t, PacketHandlerFunc> mHandlers;

        // Register all packet handlers
        void RegisterHandlers();

        // Individual packet handlers
        void HandleServerPongResponse(Core::Session* session, const Core::PKT_ServerPongRes* packet);
        void HandleDBSavePingTimeResponse(Core::Session* session, const Core::PKT_DBSavePingTimeRes* packet);

    private:
        // Ping sequence counter. Atomic because SendPingToDBServer() may be
        //          called from a timer thread while I/O callbacks run on worker threads.
        std::atomic<uint32_t> mPingSequence;
    };

} // namespace Network::TestServer
