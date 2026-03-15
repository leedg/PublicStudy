#pragma once

// Binary packet definitions for network framing

#include <cstdint>
#include <limits>

namespace Network::Core
{
// =============================================================================
// Packet type IDs
// =============================================================================

enum class PacketType : uint16_t
{
	// Session connect request (Client -> Server)
	SessionConnectReq = 0x0001,

	// Session connect response (Server -> Client)
	SessionConnectRes = 0x0002,

	// Ping request (Client -> Server)
	PingReq = 0x0003,

	// Pong response (Server -> Client)
	PongRes = 0x0004,
};

// =============================================================================
// Packet header (common to all packets)
// =============================================================================

#pragma pack(push, 1)

struct PacketHeader
{
	uint16_t size; // Total packet size (including header)
	uint16_t id; // Packet type ID

	PacketHeader() : size(0), id(0) {}

	PacketHeader(uint16_t packetSize, PacketType packetType)
		: size(packetSize), id(static_cast<uint16_t>(packetType))
	{
	}
};

static_assert(sizeof(PacketHeader) == 4, "PacketHeader must be 4 bytes");

// =============================================================================
// Session connect request packet
// =============================================================================

struct PKT_SessionConnectReq
{
	PacketHeader header;
	uint32_t clientVersion;

	PKT_SessionConnectReq()
		: header(sizeof(PKT_SessionConnectReq), PacketType::SessionConnectReq),
		  clientVersion(0)
	{
	}
};

// =============================================================================
// Session connect response packet
// =============================================================================

enum class ConnectResult : uint8_t
{
	Success = 0,
	VersionMismatch = 1,
	ServerFull = 2,
	Banned = 3,
	Unknown = 255,
};

struct PKT_SessionConnectRes
{
	PacketHeader header;
	uint64_t sessionId;
	uint32_t serverTime; // Unix timestamp
	uint8_t result;      // ConnectResult

	PKT_SessionConnectRes()
		: header(sizeof(PKT_SessionConnectRes), PacketType::SessionConnectRes),
		  sessionId(0), serverTime(0),
		  result(static_cast<uint8_t>(ConnectResult::Success))
	{
	}
};

// =============================================================================
// Ping request packet
// =============================================================================

struct PKT_PingReq
{
	PacketHeader header;
	uint64_t clientTime; // Client timestamp (ms)
	uint32_t sequence; // Sequence number

	PKT_PingReq()
		: header(sizeof(PKT_PingReq), PacketType::PingReq), clientTime(0),
		  sequence(0)
	{
	}
};

// =============================================================================
// Pong response packet
// =============================================================================

struct PKT_PongRes
{
	PacketHeader header;
	uint64_t
		clientTime; // Echo of client time
	uint64_t
		serverTime; // Server timestamp (ms)
	uint32_t sequence; // Echo of sequence

	PKT_PongRes()
		: header(sizeof(PKT_PongRes), PacketType::PongRes), clientTime(0),
		  serverTime(0), sequence(0)
	{
	}
};

#pragma pack(pop)

// =============================================================================
// Network constants
// =============================================================================

constexpr uint32_t MAX_PACKET_SIZE = 4096;
constexpr uint32_t RECV_BUFFER_SIZE = 8192;
constexpr uint32_t SEND_BUFFER_SIZE = 8192;

static_assert(SEND_BUFFER_SIZE == RECV_BUFFER_SIZE,
    "SEND_BUFFER_SIZE must equal RECV_BUFFER_SIZE: IOContext::buffer uses RECV_BUFFER_SIZE "
    "but Send() validates against SEND_BUFFER_SIZE. Mismatch causes buffer overflow.");
constexpr uint32_t PING_INTERVAL_MS = 5000;
constexpr uint32_t PING_TIMEOUT_MS = 30000;

constexpr size_t MAX_SEND_QUEUE_DEPTH = 1000;
constexpr size_t MAX_LOGIC_QUEUE_DEPTH = 10000;

constexpr uint32_t PACKET_HEADER_SIZE      = sizeof(PacketHeader);
constexpr uint32_t MAX_PACKET_TOTAL_SIZE   = MAX_PACKET_SIZE;
constexpr uint32_t MAX_PACKET_PAYLOAD_SIZE = MAX_PACKET_SIZE - PACKET_HEADER_SIZE;

static_assert(MAX_PACKET_SIZE > PACKET_HEADER_SIZE,
    "MAX_PACKET_SIZE must be > PACKET_HEADER_SIZE");
static_assert(MAX_PACKET_SIZE <= (std::numeric_limits<uint16_t>::max)(),
    "MAX_PACKET_SIZE exceeds uint16_t; update PacketHeader::size to uint32_t "
    "or reduce MAX_PACKET_SIZE");
static_assert(MAX_PACKET_SIZE <= SEND_BUFFER_SIZE,
    "MAX_PACKET_SIZE must fit within SEND_BUFFER_SIZE");

} // namespace Network::Core
