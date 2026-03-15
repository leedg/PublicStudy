// DB Server packet handler implementation

#include "../include/DBServerPacketHandler.h"
#include "Utils/PingPongConfig.h"
#include "Utils/StringUtil.h"
#include <chrono>

namespace Network::TestServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    DBServerPacketHandler::DBServerPacketHandler()
        : mPingSequence(0)
    {
        // Register all packet handlers
        RegisterHandlers();
    }

    DBServerPacketHandler::~DBServerPacketHandler()
    {
    }

    void DBServerPacketHandler::RegisterHandlers()
    {
        // Register handler functors for each packet type
        mHandlers[static_cast<uint16_t>(ServerPacketType::ServerPongRes)] =
            [this](Core::Session* session, const char* data, uint32_t size)
            {
                HandleServerPongResponse(session, reinterpret_cast<const PKT_ServerPongRes*>(data));
            };

        mHandlers[static_cast<uint16_t>(ServerPacketType::DBSavePingTimeRes)] =
            [this](Core::Session* session, const char* data, uint32_t size)
            {
                HandleDBSavePingTimeResponse(session, reinterpret_cast<const PKT_DBSavePingTimeRes*>(data));
            };
    }

    void DBServerPacketHandler::ProcessPacket(Core::Session* session, const char* data, uint32_t size)
    {
        if (!session || !data || size < sizeof(ServerPacketHeader))
        {
            Logger::Warn("Invalid DB server packet data");
            return;
        }

        const ServerPacketHeader* header = reinterpret_cast<const ServerPacketHeader*>(data);

        if (header->size < sizeof(ServerPacketHeader) || header->size > 4096)
        {
            Logger::Warn("DB server packet size out of range: " + std::to_string(header->size));
            return;
        }

        if (header->size > size)
        {
            Logger::Warn("Incomplete DB server packet - expected: " + std::to_string(header->size) +
                ", received: " + std::to_string(size));
            return;
        }

        // Validate minimal packet size per server packet id.
        uint32_t requiredSize = sizeof(ServerPacketHeader);
        switch (static_cast<ServerPacketType>(header->id))
        {
        case ServerPacketType::ServerPongRes:
            requiredSize = sizeof(PKT_ServerPongRes);
            break;
        case ServerPacketType::DBSavePingTimeRes:
            requiredSize = sizeof(PKT_DBSavePingTimeRes);
            break;
        default:
            break;
        }

        if (header->size < requiredSize)
        {
            Logger::Warn("DB server packet too small for id " + std::to_string(header->id) +
                " - expected at least: " + std::to_string(requiredSize) +
                ", actual: " + std::to_string(header->size));
            return;
        }

        // Use functor map to dispatch packet handler
        auto it = mHandlers.find(header->id);
        if (it != mHandlers.end())
        {
            it->second(session, data, size);
        }
        else
        {
            Logger::Warn("Unknown packet type from DB server: " + std::to_string(header->id));
        }
    }

    void DBServerPacketHandler::SendPingToDBServer(Core::Session* session)
    {
        if (!session)
        {
            Logger::Error("Cannot send ping - invalid session");
            return;
        }

        PKT_ServerPingReq packet;
        packet.sequence = mPingSequence.fetch_add(1, std::memory_order_relaxed) + 1;
        packet.timestamp = Timer::GetCurrentTimestamp();

        session->Send(packet);

#ifdef ENABLE_PINGPONG_VERBOSE_LOG
        Logger::Debug("Sent ping to DB server - Seq: " + std::to_string(packet.sequence));
#else
        if (packet.sequence % PINGPONG_LOG_INTERVAL == 0)
        {
            Logger::Info("[GameServer->DB] Ping sent (every " + std::to_string(PINGPONG_LOG_INTERVAL) +
                "th) - Seq: " + std::to_string(packet.sequence));
        }
#endif
    }

    void DBServerPacketHandler::RequestSavePingTime(Core::Session* session, uint32_t serverId, const char* serverName)
    {
        if (!session)
        {
            Logger::Error("Cannot save ping time - invalid session");
            return;
        }

        PKT_DBSavePingTimeReq packet;
        packet.serverId = serverId;
        packet.timestamp = Timer::GetCurrentTimestamp();

        if (serverName)
        {
            StringUtil::Copy(packet.serverName, serverName);
        }

        session->Send(packet);

        Logger::Info("Requested save ping time to DB - ServerId: " + std::to_string(serverId));
    }

    void DBServerPacketHandler::HandleServerPongResponse(Core::Session* session, const PKT_ServerPongRes* packet)
    {
        // Validate pointers
        if (!session || !packet)
        {
            Logger::Error("HandleServerPongResponse: null pointer");
            return;
        }

        uint64_t currentTime = Timer::GetCurrentTimestamp();
        uint64_t rtt = currentTime - packet->requestTimestamp;

#ifdef ENABLE_PINGPONG_VERBOSE_LOG
        Logger::Info("Received pong from DB server - Seq: " + std::to_string(packet->sequence) +
            ", RTT: " + std::to_string(rtt) + "ms");
#else
        if (packet->sequence % PINGPONG_LOG_INTERVAL == 0)
        {
            Logger::Info("[GameServer<-DB] Pong received (every " + std::to_string(PINGPONG_LOG_INTERVAL) +
                "th) - Seq: " + std::to_string(packet->sequence) +
                ", RTT: " + std::to_string(rtt) + "ms");
        }
#endif

        // Update session's last ping time
        if (session->IsConnected())
        {
            session->SetLastPingTime(packet->responseTimestamp);
        }
    }

    void DBServerPacketHandler::HandleDBSavePingTimeResponse(Core::Session* session, const PKT_DBSavePingTimeRes* packet)
    {
        // Validate pointers
        if (!session || !packet)
        {
            Logger::Error("HandleDBSavePingTimeResponse: null pointer");
            return;
        }

        if (packet->result == 0)
        {
            Logger::Info("Ping time saved successfully in DB - ServerId: " + std::to_string(packet->serverId));
        }
        else
        {
            Logger::Error("Failed to save ping time in DB - ServerId: " + std::to_string(packet->serverId) +
                ", Error: " + std::string(packet->message));
        }
    }

} // namespace Network::TestServer
