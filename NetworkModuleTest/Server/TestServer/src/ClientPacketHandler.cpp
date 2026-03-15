// Client packet handler implementation

#include "../include/ClientPacketHandler.h"
#include "Utils/PingPongConfig.h"
#include <ctime>

namespace Network::TestServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    ClientPacketHandler::ClientPacketHandler()
    {
        // Register all packet handlers on construction
        RegisterHandlers();
    }

    ClientPacketHandler::~ClientPacketHandler()
    {
    }

    void ClientPacketHandler::RegisterHandlers()
    {
        // Register SessionConnectReq handler
        mHandlers[static_cast<uint16_t>(PacketType::SessionConnectReq)] =
            [this](Core::Session* session, const char* data, uint32_t size)
            {
                HandleConnectRequest(session, reinterpret_cast<const PKT_SessionConnectReq*>(data));
            };

        // Register PingReq handler
        mHandlers[static_cast<uint16_t>(PacketType::PingReq)] =
            [this](Core::Session* session, const char* data, uint32_t size)
            {
                HandlePingRequest(session, reinterpret_cast<const PKT_PingReq*>(data));
            };
    }

    void ClientPacketHandler::ProcessPacket(Core::Session* session, const char* data, uint32_t size)
    {
        if (!session || !data || size < sizeof(PacketHeader))
        {
            Logger::Warn("Invalid packet data");
            return;
        }

        const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data);

        if (header->size < sizeof(PacketHeader) || header->size > MAX_PACKET_SIZE)
        {
            Logger::Warn("Packet size out of range: " + std::to_string(header->size));
            return;
        }

        if (header->size > size)
        {
            Logger::Warn("Incomplete packet - expected: " + std::to_string(header->size) +
                ", received: " + std::to_string(size));
            return;
        }

        // Validate minimal payload size per packet id before reinterpret_cast in handlers.
        uint32_t requiredSize = sizeof(PacketHeader);
        switch (static_cast<PacketType>(header->id))
        {
        case PacketType::SessionConnectReq:
            requiredSize = sizeof(PKT_SessionConnectReq);
            break;
        case PacketType::PingReq:
            requiredSize = sizeof(PKT_PingReq);
            break;
        default:
            break;
        }

        if (header->size < requiredSize)
        {
            Logger::Warn("Packet too small for id " + std::to_string(header->id) +
                " - expected at least: " + std::to_string(requiredSize) +
                ", actual: " + std::to_string(header->size));
            return;
        }

        // Dispatch to handler using functor map
        auto it = mHandlers.find(header->id);
        if (it != mHandlers.end())
        {
            it->second(session, data, size);
        }
        else
        {
            Logger::Warn("Unknown packet type from client: " + std::to_string(header->id));
        }
    }

    void ClientPacketHandler::HandleConnectRequest(Core::Session* session, const PKT_SessionConnectReq* packet)
    {
        // Validate pointers
        if (!session || !packet)
        {
            Logger::Error("HandleConnectRequest: null pointer");
            return;
        }

        Logger::Info("Client connect request - Session: " + std::to_string(session->GetId()) +
            ", ClientVersion: " + std::to_string(packet->clientVersion));

        // Send connect response
        PKT_SessionConnectRes response;
        response.sessionId = session->GetId();
        response.serverTime = static_cast<uint32_t>(std::time(nullptr));
        response.result = static_cast<uint8_t>(ConnectResult::Success);

        session->Send(response);
    }

    void ClientPacketHandler::HandlePingRequest(Core::Session* session, const PKT_PingReq* packet)
    {
        // Validate pointers
        if (!session || !packet)
        {
            Logger::Error("HandlePingRequest: null pointer");
            return;
        }

        session->SetLastPingTime(Timer::GetCurrentTimestamp());

        // Send pong response
        PKT_PongRes response;
        response.clientTime = packet->clientTime;
        response.serverTime = Timer::GetCurrentTimestamp();
        response.sequence = packet->sequence;

        session->Send(response);

#ifdef ENABLE_PINGPONG_VERBOSE_LOG
        Logger::Debug("Client Ping/Pong - Session: " + std::to_string(session->GetId()) +
            ", Seq: " + std::to_string(packet->sequence));
#else
        if (packet->sequence % PINGPONG_LOG_INTERVAL == 0)
        {
            Logger::Info("[GameServer] Client Ping/Pong (every " + std::to_string(PINGPONG_LOG_INTERVAL) +
                "th) - Session: " + std::to_string(session->GetId()) +
                ", Seq: " + std::to_string(packet->sequence));
        }
#endif
    }

} // namespace Network::TestServer
