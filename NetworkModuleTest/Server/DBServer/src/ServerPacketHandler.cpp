// English: Server packet handler implementation
// 한글: 서버 패킷 핸들러 구현

#include "../include/ServerPacketHandler.h"
#include <cstring>

namespace Network::DBServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    ServerPacketHandler::ServerPacketHandler()
        : mDBPingTimeManager(nullptr)
    {
        // English: Register all packet handlers
        // 한글: 모든 패킷 핸들러 등록
        RegisterHandlers();
    }

    ServerPacketHandler::~ServerPacketHandler()
    {
    }

    void ServerPacketHandler::Initialize(DBPingTimeManager* dbPingTimeManager)
    {
        mDBPingTimeManager = dbPingTimeManager;
    }

    void ServerPacketHandler::RegisterHandlers()
    {
        // English: Register handler functors for each packet type
        // 한글: 각 패킷 타입에 대한 핸들러 펑터 등록
        mHandlers[static_cast<uint16_t>(ServerPacketType::ServerPingReq)] =
            [this](Core::Session* session, const char* data, uint32_t size)
            {
                HandleServerPingRequest(session, reinterpret_cast<const PKT_ServerPingReq*>(data));
            };

        mHandlers[static_cast<uint16_t>(ServerPacketType::DBSavePingTimeReq)] =
            [this](Core::Session* session, const char* data, uint32_t size)
            {
                HandleDBSavePingTimeRequest(session, reinterpret_cast<const PKT_DBSavePingTimeReq*>(data));
            };
    }

    void ServerPacketHandler::ProcessPacket(Core::Session* session, const char* data, uint32_t size)
    {
        if (!session || !data || size < sizeof(ServerPacketHeader))
        {
            Logger::Warn("Invalid server packet data");
            return;
        }

        const ServerPacketHeader* header = reinterpret_cast<const ServerPacketHeader*>(data);

        if (header->size > size)
        {
            Logger::Warn("Incomplete server packet - expected: " + std::to_string(header->size) +
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
            Logger::Warn("Unknown packet type from game server: " + std::to_string(header->id));
        }
    }

    void ServerPacketHandler::HandleServerPingRequest(Core::Session* session, const PKT_ServerPingReq* packet)
    {
        Logger::Debug("Server ping received - Seq: " + std::to_string(packet->sequence));

        // English: Send pong response
        // 한글: 퐁 응답 전송
        PKT_ServerPongRes response;
        response.sequence = packet->sequence;
        response.requestTimestamp = packet->timestamp;
        response.responseTimestamp = Timer::GetCurrentTimestamp();

        session->Send(response);

        Logger::Debug("Server pong sent - Seq: " + std::to_string(packet->sequence));
    }

    void ServerPacketHandler::HandleDBSavePingTimeRequest(Core::Session* session, const PKT_DBSavePingTimeReq* packet)
    {
        Logger::Info("DB save ping time request - ServerId: " + std::to_string(packet->serverId) +
            ", ServerName: " + std::string(packet->serverName));

        PKT_DBSavePingTimeRes response;
        response.serverId = packet->serverId;

        // English: Save ping time using DB manager
        // 한글: DB 관리자를 사용하여 ping 시간 저장
        if (mDBPingTimeManager && mDBPingTimeManager->IsInitialized())
        {
            bool success = mDBPingTimeManager->SavePingTime(
                packet->serverId,
                std::string(packet->serverName),
                packet->timestamp
            );

            if (success)
            {
                response.result = 0;  // Success
#ifdef _WIN32
                strncpy_s(response.message, sizeof(response.message), "Ping time saved successfully", _TRUNCATE);
#else
                strncpy(response.message, "Ping time saved successfully", sizeof(response.message) - 1);
                response.message[sizeof(response.message) - 1] = '\0';
#endif
                Logger::Info("Ping time saved for ServerId: " + std::to_string(packet->serverId));
            }
            else
            {
                response.result = 1;  // Failure
#ifdef _WIN32
                strncpy_s(response.message, sizeof(response.message), "Failed to save ping time", _TRUNCATE);
#else
                strncpy(response.message, "Failed to save ping time", sizeof(response.message) - 1);
                response.message[sizeof(response.message) - 1] = '\0';
#endif
                Logger::Error("Failed to save ping time for ServerId: " + std::to_string(packet->serverId));
            }
        }
        else
        {
            response.result = 2;  // DB manager not available
#ifdef _WIN32
            strncpy_s(response.message, sizeof(response.message), "DB manager not initialized", _TRUNCATE);
#else
            strncpy(response.message, "DB manager not initialized", sizeof(response.message) - 1);
            response.message[sizeof(response.message) - 1] = '\0';
#endif
            Logger::Error("DB manager not initialized");
        }

        // English: Send response back to game server
        // 한글: 게임 서버로 응답 전송
        session->Send(response);
    }

} // namespace Network::DBServer
