#pragma once

// English: Binary packet definitions for network framing
// ?쒓?: ?ㅽ듃?뚰겕 ?꾨젅?대컢??諛붿씠?덈━ ?⑦궥 ?뺤쓽

#include <cstdint>

namespace Network::Core
{
    // =============================================================================
    // English: Packet type IDs
    // ?쒓?: ?⑦궥 ???ID
    // =============================================================================

    enum class PacketType : uint16_t
    {
        // English: Session connect request (Client -> Server)
        // ?쒓?: ?몄뀡 ?곌껐 ?붿껌 (?대씪?댁뼵??-> ?쒕쾭)
        SessionConnectReq   = 0x0001,

        // English: Session connect response (Server -> Client)
        // ?쒓?: ?몄뀡 ?곌껐 ?묐떟 (?쒕쾭 -> ?대씪?댁뼵??
        SessionConnectRes   = 0x0002,

        // English: Ping request (Client -> Server)
        // ?쒓?: ???붿껌 (?대씪?댁뼵??-> ?쒕쾭)
        PingReq             = 0x0003,

        // English: Pong response (Server -> Client)
        // ?쒓?: ???묐떟 (?쒕쾭 -> ?대씪?댁뼵??
        PongRes             = 0x0004,
    };

    // =============================================================================
    // English: Packet header (common to all packets)
    // ?쒓?: ?⑦궥 ?ㅻ뜑 (紐⑤뱺 ?⑦궥??怨듯넻 ?ㅻ뜑)
    // =============================================================================

#pragma pack(push, 1)

    struct PacketHeader
    {
        uint16_t size;      // English: Total packet size (including header) / ?쒓?: ?⑦궥 ?꾩껜 ?ш린 (?ㅻ뜑 ?ы븿)
        uint16_t id;        // English: Packet type ID / ?쒓?: ?⑦궥 ???ID

        PacketHeader()
            : size(0)
            , id(0)
        {
        }

        PacketHeader(uint16_t packetSize, PacketType packetType)
            : size(packetSize)
            , id(static_cast<uint16_t>(packetType))
        {
        }
    };

    static_assert(sizeof(PacketHeader) == 4, "PacketHeader must be 4 bytes");

    // =============================================================================
    // English: Session connect request packet
    // ?쒓?: ?몄뀡 ?곌껐 ?붿껌 ?⑦궥
    // =============================================================================

    struct PKT_SessionConnectReq
    {
        PacketHeader header;
        uint32_t clientVersion;

        PKT_SessionConnectReq()
            : header(sizeof(PKT_SessionConnectReq), PacketType::SessionConnectReq)
            , clientVersion(0)
        {
        }
    };

    // =============================================================================
    // English: Session connect response packet
    // ?쒓?: ?몄뀡 ?곌껐 ?묐떟 ?⑦궥
    // =============================================================================

    enum class ConnectResult : uint8_t
    {
        Success         = 0,
        VersionMismatch = 1,
        ServerFull      = 2,
        Banned          = 3,
        Unknown         = 255,
    };

    struct PKT_SessionConnectRes
    {
        PacketHeader header;
        uint64_t sessionId;
        uint32_t serverTime;        // English: Unix timestamp / ?쒓?: ?좊땳????꾩뒪?ы봽
        uint8_t  result;            // English: ConnectResult / ?쒓?: ?곌껐 寃곌낵

        PKT_SessionConnectRes()
            : header(sizeof(PKT_SessionConnectRes), PacketType::SessionConnectRes)
            , sessionId(0)
            , serverTime(0)
            , result(static_cast<uint8_t>(ConnectResult::Success))
        {
        }
    };

    // =============================================================================
    // English: Ping request packet
    // ?쒓?: ???붿껌 ?⑦궥
    // =============================================================================

    struct PKT_PingReq
    {
        PacketHeader header;
        uint64_t clientTime;        // English: Client timestamp (ms) / ?쒓?: ?대씪?댁뼵???쒓컙 (諛由ъ큹)
        uint32_t sequence;          // English: Sequence number / ?쒓?: ?쒗??踰덊샇

        PKT_PingReq()
            : header(sizeof(PKT_PingReq), PacketType::PingReq)
            , clientTime(0)
            , sequence(0)
        {
        }
    };

    // =============================================================================
    // English: Pong response packet
    // ?쒓?: ???묐떟 ?⑦궥
    // =============================================================================

    struct PKT_PongRes
    {
        PacketHeader header;
        uint64_t clientTime;        // English: Echo of client time / ?쒓?: ?대씪?댁뼵???쒓컙 ?먯퐫
        uint64_t serverTime;        // English: Server timestamp (ms) / ?쒓?: ?쒕쾭 ?쒓컙 (諛由ъ큹)
        uint32_t sequence;          // English: Echo of sequence / ?쒓?: ?쒗???먯퐫

        PKT_PongRes()
            : header(sizeof(PKT_PongRes), PacketType::PongRes)
            , clientTime(0)
            , serverTime(0)
            , sequence(0)
        {
        }
    };

#pragma pack(pop)

    // =============================================================================
    // English: Network constants
    // ?쒓?: ?ㅽ듃?뚰겕 ?곸닔
    // =============================================================================

    constexpr uint32_t MAX_PACKET_SIZE      = 4096;
    constexpr uint32_t RECV_BUFFER_SIZE     = 8192;
    constexpr uint32_t SEND_BUFFER_SIZE     = 8192;
    constexpr uint32_t PING_INTERVAL_MS     = 5000;
    constexpr uint32_t PING_TIMEOUT_MS      = 30000;

} // namespace Network::Core

