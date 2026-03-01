#pragma once

// English: Binary packet definitions for network framing
// 한글: 네트워크 프레이밍용 바이너리 패킷 정의

#include <cstdint>
#include <limits>

namespace Network::Core
{
// =============================================================================
// English: Packet type IDs
// 한글: 패킷 타입 ID
// =============================================================================

enum class PacketType : uint16_t
{
	// English: Session connect request (Client -> Server)
	// 한글: 세션 연결 요청 (클라이언트 -> 서버)
	SessionConnectReq = 0x0001,

	// English: Session connect response (Server -> Client)
	// 한글: 세션 연결 응답 (서버 -> 클라이언트)
	SessionConnectRes = 0x0002,

	// English: Ping request (Client -> Server)
	// 한글: 핑 요청 (클라이언트 -> 서버)
	PingReq = 0x0003,

	// English: Pong response (Server -> Client)
	// 한글: 퐁 응답 (서버 -> 클라이언트)
	PongRes = 0x0004,
};

// =============================================================================
// English: Packet header (common to all packets)
// 한글: 패킷 헤더 (모든 패킷의 공통 헤더)
// =============================================================================

#pragma pack(push, 1)

struct PacketHeader
{
	uint16_t size; // English: Total packet size (including header) / 한글: 패킷
					   // 전체 크기 (헤더 포함)
	uint16_t id; // English: Packet type ID / 한글: 패킷 타입 ID

	PacketHeader() : size(0), id(0) {}

	PacketHeader(uint16_t packetSize, PacketType packetType)
		: size(packetSize), id(static_cast<uint16_t>(packetType))
	{
	}
};

static_assert(sizeof(PacketHeader) == 4, "PacketHeader must be 4 bytes");

// =============================================================================
// English: Session connect request packet
// 한글: 세션 연결 요청 패킷
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
// English: Session connect response packet
// 한글: 세션 연결 응답 패킷
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
	uint32_t serverTime; // English: Unix timestamp / 한글: 유닉스 타임스탬프
	uint8_t result;      // English: ConnectResult / 한글: 연결 결과

	PKT_SessionConnectRes()
		: header(sizeof(PKT_SessionConnectRes), PacketType::SessionConnectRes),
		  sessionId(0), serverTime(0),
		  result(static_cast<uint8_t>(ConnectResult::Success))
	{
	}
};

// =============================================================================
// English: Ping request packet
// 한글: 핑 요청 패킷
// =============================================================================

struct PKT_PingReq
{
	PacketHeader header;
	uint64_t clientTime; // English: Client timestamp (ms) / 한글: 클라이언트
						 // 시간 (밀리초)
	uint32_t sequence; // English: Sequence number / 한글: 시퀀스 번호

	PKT_PingReq()
		: header(sizeof(PKT_PingReq), PacketType::PingReq), clientTime(0),
		  sequence(0)
	{
	}
};

// =============================================================================
// English: Pong response packet
// 한글: 퐁 응답 패킷
// =============================================================================

struct PKT_PongRes
{
	PacketHeader header;
	uint64_t
		clientTime; // English: Echo of client time / 한글: 클라이언트 시간 에코
	uint64_t
		serverTime; // English: Server timestamp (ms) / 한글: 서버 시간 (밀리초)
	uint32_t sequence; // English: Echo of sequence / 한글: 시퀀스 에코

	PKT_PongRes()
		: header(sizeof(PKT_PongRes), PacketType::PongRes), clientTime(0),
		  serverTime(0), sequence(0)
	{
	}
};

#pragma pack(pop)

// =============================================================================
// English: Network constants
// 한글: 네트워크 상수
// =============================================================================

constexpr uint32_t MAX_PACKET_SIZE = 4096;
constexpr uint32_t RECV_BUFFER_SIZE = 8192;
constexpr uint32_t SEND_BUFFER_SIZE = 8192;

// [Fix A-2] IOContext::buffer는 RECV_BUFFER_SIZE를 기준으로 정의됨.
// Send 경로는 SEND_BUFFER_SIZE로 검증하므로, 두 값이 다를 경우 버퍼 오버플로우가 발생한다.
// 상수 수정 시 반드시 두 값을 함께 조정해야 한다.
static_assert(SEND_BUFFER_SIZE == RECV_BUFFER_SIZE,
    "SEND_BUFFER_SIZE must equal RECV_BUFFER_SIZE: IOContext::buffer uses RECV_BUFFER_SIZE "
    "but Send() validates against SEND_BUFFER_SIZE. Mismatch causes buffer overflow.");
constexpr uint32_t PING_INTERVAL_MS = 5000;
constexpr uint32_t PING_TIMEOUT_MS = 30000;

constexpr size_t MAX_SEND_QUEUE_DEPTH = 1000;
constexpr size_t MAX_LOGIC_QUEUE_DEPTH = 10000;

// ─── 패킷 크기 명시적 상수 ────────────────────────────────────────────────────
// MAX_PACKET_SIZE = 전체 와이어 크기 (PacketHeader + payload 합산)
constexpr uint32_t PACKET_HEADER_SIZE      = sizeof(PacketHeader);
constexpr uint32_t MAX_PACKET_TOTAL_SIZE   = MAX_PACKET_SIZE;   // 명시적 alias
constexpr uint32_t MAX_PACKET_PAYLOAD_SIZE = MAX_PACKET_SIZE - PACKET_HEADER_SIZE;

// ─── 컴파일 타임 불변식 ──────────────────────────────────────────────────────
static_assert(MAX_PACKET_SIZE > PACKET_HEADER_SIZE,
    "MAX_PACKET_SIZE must be > PACKET_HEADER_SIZE");
static_assert(MAX_PACKET_SIZE <= std::numeric_limits<uint16_t>::max(),
    "MAX_PACKET_SIZE exceeds uint16_t; update PacketHeader::size to uint32_t "
    "or reduce MAX_PACKET_SIZE");
static_assert(MAX_PACKET_SIZE <= SEND_BUFFER_SIZE,
    "MAX_PACKET_SIZE must fit within SEND_BUFFER_SIZE");

} // namespace Network::Core
