// English: DB Server packet handler implementation
// 한글: DB 서버 패킷 핸들러 구현

#include "../include/DBServerPacketHandler.h"
#include <cstring>
#include <chrono>

namespace Network::TestServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    DBServerPacketHandler::DBServerPacketHandler()
        : mPingSequence(0)
    {
    }

    DBServerPacketHandler::~DBServerPacketHandler()
    {
    }

    void DBServerPacketHandler::ProcessPacket(Core::Session* session, const char* data, uint32_t size)
    {
        if (!session || !data || size < sizeof(ServerPacketHeader))
        {
            Logger::Warn("Invalid DB server packet data");
            return;
        }

        const ServerPacketHeader* header = reinterpret_cast<const ServerPacketHeader*>(data);

        if (header->size > size)
        {
            Logger::Warn("Incomplete DB server packet - expected: " + std::to_string(header->size) +
                ", received: " + std::to_string(size));
            return;
        }

        ServerPacketType packetType = static_cast<ServerPacketType>(header->id);

        switch (packetType)
        {
        case ServerPacketType::ServerPongRes:
            HandleServerPongResponse(session, reinterpret_cast<const PKT_ServerPongRes*>(data));
            break;

        case ServerPacketType::DBSavePingTimeRes:
            HandleDBSavePingTimeResponse(session, reinterpret_cast<const PKT_DBSavePingTimeRes*>(data));
            break;

        default:
            Logger::Warn("Unknown packet type from DB server: " + std::to_string(header->id));
            break;
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
        packet.sequence = ++mPingSequence;
        packet.timestamp = Timer::GetCurrentTimestamp();

        session->Send(packet);

        Logger::Debug("Sent ping to DB server - Seq: " + std::to_string(packet.sequence));
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
#ifdef _WIN32
            strncpy_s(packet.serverName, sizeof(packet.serverName), serverName, _TRUNCATE);
#else
            strncpy(packet.serverName, serverName, sizeof(packet.serverName) - 1);
            packet.serverName[sizeof(packet.serverName) - 1] = '\0';
#endif
        }

        session->Send(packet);

        Logger::Info("Requested save ping time to DB - ServerId: " + std::to_string(serverId));
    }

    void DBServerPacketHandler::HandleServerPongResponse(Core::Session* session, const PKT_ServerPongRes* packet)
    {
        uint64_t currentTime = Timer::GetCurrentTimestamp();
        uint64_t rtt = currentTime - packet->requestTimestamp;

        Logger::Info("Received pong from DB server - Seq: " + std::to_string(packet->sequence) +
            ", RTT: " + std::to_string(rtt) + "ms");

        // English: Update session's last ping time
        // 한글: 세션의 마지막 Ping 시간 갱신
        session->SetLastPingTime(packet->responseTimestamp);
    }

    void DBServerPacketHandler::HandleDBSavePingTimeResponse(Core::Session* session, const PKT_DBSavePingTimeRes* packet)
    {
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
