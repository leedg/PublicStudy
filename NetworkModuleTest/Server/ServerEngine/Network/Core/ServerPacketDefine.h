#pragma once

// Server-to-Server packet definitions

#include <cstdint>
#include <ctime>

// Disable padding for network packets to ensure consistent byte layout
#pragma pack(push, 1)

namespace Network::Core
{
    // =============================================================================
    // Server packet types
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
    // =============================================================================

    struct PKT_DBQueryReq
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBQueryReq;

        ServerPacketHeader header;
        uint32_t queryId;        // Query identifier
        uint16_t queryLength;    // Length of query string
        char query[512];         // SQL query (null-terminated)

        PKT_DBQueryReq() : queryId(0), queryLength(0)
        {
            header.InitPacket<PKT_DBQueryReq>();
            query[0] = '\0';
        }
    };

    struct PKT_DBQueryRes
    {
        static constexpr ServerPacketType PacketId = ServerPacketType::DBQueryRes;

        ServerPacketHeader header;
        uint32_t queryId;        // Matching query identifier
        uint8_t result;          // 0 = success, non-zero = error code
        uint16_t dataLength;     // Length of result data
        char data[1024];         // Result data (null-terminated JSON or other format)

        PKT_DBQueryRes() : queryId(0), result(0), dataLength(0)
        {
            header.InitPacket<PKT_DBQueryRes>();
            data[0] = '\0';
        }
    };

} // namespace Network::Core

// Restore default packing
#pragma pack(pop)
