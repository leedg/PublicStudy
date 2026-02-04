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
        // English: Register all packet handlers
        // 한글: 모든 패킷 핸들러 등록
        RegisterHandlers();
    }

    DBServerPacketHandler::~DBServerPacketHandler()
    {
    }

    void DBServerPacketHandler::RegisterHandlers()
    {
        // English: Register handler functors for each packet type
        // 한글: 각 패킷 타입에 대한 핸들러 펑터 등록
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

        if (header->size > size)
        {
            Logger::Warn("Incomplete DB server packet - expected: " + std::to_string(header->size) +
                ", received: " + std::to_string(size));
            return;
        }

        // English: Use functor map to dispatch packet handler
        // 한글: 펑터 맵을 사용하여 패킷 핸들러 디스패치
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
        // English: Validate pointers
        // 한글: 포인터 유효성 검사
        if (!session || !packet)
        {
            Logger::Error("HandleServerPongResponse: null pointer");
            return;
        }

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
        // English: Validate pointers
        // 한글: 포인터 유효성 검사
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
