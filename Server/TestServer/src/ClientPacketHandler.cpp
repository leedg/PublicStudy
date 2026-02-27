// English: Client packet handler implementation
// 한글: 클라이언트 패킷 핸들러 구현

#include "../include/ClientPacketHandler.h"
#include "Utils/PingPongConfig.h"
#include <ctime>

namespace Network::TestServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    ClientPacketHandler::ClientPacketHandler()
    {
        // English: Register all packet handlers on construction
        // 한글: 생성 시 모든 패킷 핸들러 등록
        RegisterHandlers();
    }

    ClientPacketHandler::~ClientPacketHandler()
    {
    }

    void ClientPacketHandler::RegisterHandlers()
    {
        // English: Register SessionConnectReq handler
        // 한글: SessionConnectReq 핸들러 등록
        mHandlers[static_cast<uint16_t>(PacketType::SessionConnectReq)] =
            [this](Core::Session* session, const char* data, uint32_t size)
            {
                HandleConnectRequest(session, reinterpret_cast<const PKT_SessionConnectReq*>(data));
            };

        // English: Register PingReq handler
        // 한글: PingReq 핸들러 등록
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

        // English: Validate minimal payload size per packet id before reinterpret_cast in handlers.
        // 한글: 핸들러 내부 reinterpret_cast 전에 패킷 ID별 최소 길이 검증.
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

        // English: Dispatch to handler using functor map
        // 한글: 펑터 맵을 사용하여 핸들러로 디스패치
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
        // English: Validate pointers
        // 한글: 포인터 유효성 검사
        if (!session || !packet)
        {
            Logger::Error("HandleConnectRequest: null pointer");
            return;
        }

        Logger::Info("Client connect request - Session: " + std::to_string(session->GetId()) +
            ", ClientVersion: " + std::to_string(packet->clientVersion));

        // English: Send connect response
        // 한글: 접속 응답 전송
        PKT_SessionConnectRes response;
        response.sessionId = session->GetId();
        response.serverTime = static_cast<uint32_t>(std::time(nullptr));
        response.result = static_cast<uint8_t>(ConnectResult::Success);

        session->Send(response);
    }

    void ClientPacketHandler::HandlePingRequest(Core::Session* session, const PKT_PingReq* packet)
    {
        // English: Validate pointers
        // 한글: 포인터 유효성 검사
        if (!session || !packet)
        {
            Logger::Error("HandlePingRequest: null pointer");
            return;
        }

        session->SetLastPingTime(Timer::GetCurrentTimestamp());

        // English: Send pong response
        // 한글: 퐁 응답 전송
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
