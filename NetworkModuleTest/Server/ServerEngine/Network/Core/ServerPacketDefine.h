#pragma once

// Server-to-Server packet definitions
// 한글: 서버 간 패킷 정의

#include <cstdint>
#include <ctime>

// Disable padding for network packets to ensure consistent byte layout
// 한글: 네트워크 패킷의 패딩을 비활성화하여 일관된 바이트 레이아웃 보장
#pragma pack(push, 1)

namespace Network::Core
{
    // =============================================================================
    // Server packet types
    // 한글: 서버 패킷 타입
    // =============================================================================

    enum class ServerPacketType : uint16_t
    {
        Invalid = 0,

        // Server-to-Server Ping/Pong
        ServerPingReq = 1000,
        ServerPongRes = 1001,

        // DB Request/Response
        DBSavePingTimeReq = 2000,
        DBSavePingTimeRes = 2001,
        DBQueryReq = 2002,
        DBQueryRes = 2003,

        Max
    };

    // =============================================================================
    // Server packet header
    // 한글: 서버 패킷 헤더
    // =============================================================================

    struct ServerPacketHeader
    {
        uint16_t size;           // Total packet size
        uint16_t id;             // ServerPacketType
        uint32_t sequence;       // Sequence number

        ServerPacketHeader()
            : size(sizeof(ServerPacketHeader))
            , id(static_cast<uint16_t>(ServerPacketType::Invalid))
            , sequence(0)
        {
        }

        template<typename T>
        void InitPacket()
        {
            size = sizeof(T);
            id = static_cast<uint16_t>(T::PacketId);
        }
    };

    // =============================================================================
    // Server Ping/Pong packets
    // 한글: 서버 Ping/Pong 패킷
    // =============================================================================

    struct PKT_ServerPingReq
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::ServerPingReq;

        ServerPacketHeader header;
        uint64_t timestamp;      // Client timestamp (milliseconds since epoch)
        uint32_t sequence;       // Sequence number for matching

        PKT_ServerPingReq() : timestamp(0), sequence(0)
        {
            header.InitPacket<PKT_ServerPingReq>();
        }
    };

    struct PKT_ServerPongRes
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::ServerPongRes;

        ServerPacketHeader header;
        uint64_t requestTimestamp;   // Original request timestamp
        uint64_t responseTimestamp;  // Server response timestamp
        uint32_t sequence;           // Matching sequence number

        PKT_ServerPongRes() : requestTimestamp(0), responseTimestamp(0), sequence(0)
        {
            header.InitPacket<PKT_ServerPongRes>();
        }
    };

    // =============================================================================
    // DB Save Ping Time packets
    // 한글: DB Ping 시간 저장 패킷
    // =============================================================================

    struct PKT_DBSavePingTimeReq
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBSavePingTimeReq;

        ServerPacketHeader header;
        uint32_t serverId;       // Server ID
        uint64_t timestamp;      // Ping timestamp in GMT (milliseconds since epoch)
        char serverName[32];     // Server name (null-terminated)

        PKT_DBSavePingTimeReq() : serverId(0), timestamp(0)
        {
            header.InitPacket<PKT_DBSavePingTimeReq>();
            serverName[0] = '\0';
        }
    };

    struct PKT_DBSavePingTimeRes
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBSavePingTimeRes;

        ServerPacketHeader header;
        uint32_t serverId;       // Server ID
        uint8_t result;          // 0 = success, non-zero = error code
        char message[64];        // Result message (null-terminated)

        PKT_DBSavePingTimeRes() : serverId(0), result(0)
        {
            header.InitPacket<PKT_DBSavePingTimeRes>();
            message[0] = '\0';
        }
    };

    // =============================================================================
    // Generic DB Query packets
    // 한글: 범용 DB 쿼리 패킷
    // =============================================================================

    struct PKT_DBQueryReq
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBQueryReq;

        ServerPacketHeader header;
        uint64_t  queryId;       // requestId (KeyGenerator::KeyId: tag|slot|seq48)
        uint8_t   taskType;      // DBServerTaskType (cast to uint8_t)
        uint16_t  dataLength;    // bytes used in data[] (not including null terminator)
        char      data[512];     // JSON payload (null-terminated, dataLength bytes valid)

        PKT_DBQueryReq() : queryId(0), taskType(0), dataLength(0)
        {
            header.InitPacket<PKT_DBQueryReq>();
            data[0] = '\0';
        }
    };

    struct PKT_DBQueryRes
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBQueryRes;

        ServerPacketHeader header;
        uint64_t  queryId;       // requestId echo (KeyGenerator::KeyId)
        int32_t   result;        // Network::ResultCode cast to int32_t
        uint16_t  detailLength;  // bytes used in detail[]
        char      detail[256];   // detail message (null-terminated)

        PKT_DBQueryRes() : queryId(0), result(0), detailLength(0)
        {
            header.InitPacket<PKT_DBQueryRes>();
            detail[0] = '\0';
        }
    };

} // namespace Network::Core

// Restore default packing
// 한글: 기본 패킹으로 복원
#pragma pack(pop)
