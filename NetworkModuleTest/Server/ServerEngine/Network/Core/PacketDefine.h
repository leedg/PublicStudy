#pragma once

// 네트워크 프레이밍용 바이너리 패킷 정의

#include <cstdint>
#include <limits>

namespace Network::Core
{
// =============================================================================
// 패킷 타입 ID
// =============================================================================

// 0x0001–0x00FF: 클라이언트↔서버 기본 프로토콜
// 0x1000 이상: 서버 간 패킷 (ServerPacketDefine.h 참고)
enum class PacketType : uint16_t
{
	SessionConnectReq = 0x0001, // 세션 연결 요청 (클라이언트 -> 서버)
	SessionConnectRes = 0x0002, // 세션 연결 응답 (서버 -> 클라이언트)
	PingReq           = 0x0003, // 핑 요청 (클라이언트 -> 서버)
	PongRes           = 0x0004, // 퐁 응답 (서버 -> 클라이언트)
};

// =============================================================================
// 패킷 헤더 (모든 패킷의 공통 헤더)
// =============================================================================

#pragma pack(push, 1)

struct PacketHeader
{
	uint16_t size; // 패킷 전체 크기 (헤더 포함). ProcessRawRecv 경계 검출에 사용.
	uint16_t id;   // 패킷 타입 ID (PacketType 열거형 값)

	PacketHeader() : size(0), id(0) {}

	PacketHeader(uint16_t packetSize, PacketType packetType)
		: size(packetSize), id(static_cast<uint16_t>(packetType))
	{
	}
};

static_assert(sizeof(PacketHeader) == 4, "PacketHeader must be 4 bytes");

// =============================================================================
// 세션 연결 요청 패킷
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
// 세션 연결 응답 패킷
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
	uint32_t serverTime; // 유닉스 타임스탬프 (초 단위, 클라이언트 시계 동기화용)
	uint8_t  result;     // ConnectResult 열거형 값

	PKT_SessionConnectRes()
		: header(sizeof(PKT_SessionConnectRes), PacketType::SessionConnectRes),
		  sessionId(0), serverTime(0),
		  result(static_cast<uint8_t>(ConnectResult::Success))
	{
	}
};

// =============================================================================
// 핑 요청 패킷
// =============================================================================

struct PKT_PingReq
{
	PacketHeader header;
	uint64_t clientTime; // 클라이언트 타임스탬프 (밀리초 단위)
	uint32_t sequence;   // 시퀀스 번호 (Pong 응답과 매칭)

	PKT_PingReq()
		: header(sizeof(PKT_PingReq), PacketType::PingReq), clientTime(0),
		  sequence(0)
	{
	}
};

// =============================================================================
// 퐁 응답 패킷
// =============================================================================

struct PKT_PongRes
{
	PacketHeader header;
	uint64_t clientTime; // 클라이언트 타임스탬프 에코 (RTT 계산용)
	uint64_t serverTime; // 서버 처리 시간 (밀리초 단위)
	uint32_t sequence;   // 시퀀스 에코 (요청과 응답 매칭)

	PKT_PongRes()
		: header(sizeof(PKT_PongRes), PacketType::PongRes), clientTime(0),
		  serverTime(0), sequence(0)
	{
	}
};

#pragma pack(pop)

// =============================================================================
// 네트워크 상수
// =============================================================================

// MAX_PACKET_SIZE = 4096: 애플리케이션 패킷 1개의 최대 와이어 크기.
// 네트워크 MTU(1500B)보다 크게 잡아 일반 게임 패킷을 수용하면서도
// 과도한 단편화를 막는 실용적 상한선이다.
constexpr uint32_t MAX_PACKET_SIZE = 4096;

// RECV/SEND_BUFFER_SIZE = 8192: MAX_PACKET_SIZE(4096)의 2배.
// 단일 recv/send 호출에서 최대 패킷 2개가 겹쳐 도달하는 경우를 수용하고,
// 또한 IOContext::buffer는 RECV_BUFFER_SIZE를 기준으로 선언되므로
// Send 경로의 SEND_BUFFER_SIZE와 반드시 같아야 한다 (아래 static_assert 참고).
constexpr uint32_t RECV_BUFFER_SIZE = 8192;
constexpr uint32_t SEND_BUFFER_SIZE = 8192;

// [Fix A-2] IOContext::buffer는 RECV_BUFFER_SIZE를 기준으로 정의됨.
// Send 경로는 SEND_BUFFER_SIZE로 검증하므로, 두 값이 다를 경우 버퍼 오버플로우가 발생한다.
// 상수 수정 시 반드시 두 값을 함께 조정해야 한다.
static_assert(SEND_BUFFER_SIZE == RECV_BUFFER_SIZE,
    "SEND_BUFFER_SIZE must equal RECV_BUFFER_SIZE: IOContext::buffer uses RECV_BUFFER_SIZE "
    "but Send() validates against SEND_BUFFER_SIZE. Mismatch causes buffer overflow.");
// PING_INTERVAL_MS = 5000ms: 클라이언트가 핑을 보내는 주기.
// PING_TIMEOUT_MS  = 30000ms: 마지막 핑 이후 이 시간이 지나면 비활성 연결로 간주하고 종료.
// 엔진 타이머는 PING_TIMEOUT_MS/2 주기로 점검하여 최악의 경우에도 30s 이내에 감지.
constexpr uint32_t PING_INTERVAL_MS = 5000;
constexpr uint32_t PING_TIMEOUT_MS = 30000;

// MAX_SEND_QUEUE_DEPTH = 1000: 세션당 미전송 패킷 한도.
// 이를 초과하면 Send()가 QueueFull을 반환하여 호출자가 흐름 제어를 직접 수행하게 한다.
constexpr size_t MAX_SEND_QUEUE_DEPTH = 1000;

// MAX_LOGIC_QUEUE_DEPTH = 10000: KeyedDispatcher 전체 큐 한도.
// 로직 워커가 처리하지 못할 만큼 패킷이 쌓이면 드롭 후 연결을 끊어 메모리 폭증을 방지.
constexpr size_t MAX_LOGIC_QUEUE_DEPTH = 10000;

// ─── 패킷 크기 명시적 상수 ────────────────────────────────────────────────────
// MAX_PACKET_SIZE = 전체 와이어 크기 (PacketHeader + payload 합산)
constexpr uint32_t PACKET_HEADER_SIZE      = sizeof(PacketHeader);
constexpr uint32_t MAX_PACKET_TOTAL_SIZE   = MAX_PACKET_SIZE;   // 명시적 alias
constexpr uint32_t MAX_PACKET_PAYLOAD_SIZE = MAX_PACKET_SIZE - PACKET_HEADER_SIZE;

// ─── 컴파일 타임 불변식 ──────────────────────────────────────────────────────
static_assert(MAX_PACKET_SIZE > PACKET_HEADER_SIZE,
    "MAX_PACKET_SIZE must be > PACKET_HEADER_SIZE");
static_assert(MAX_PACKET_SIZE <= (std::numeric_limits<uint16_t>::max)(),
    "MAX_PACKET_SIZE exceeds uint16_t; update PacketHeader::size to uint32_t "
    "or reduce MAX_PACKET_SIZE");
static_assert(MAX_PACKET_SIZE <= SEND_BUFFER_SIZE,
    "MAX_PACKET_SIZE must fit within SEND_BUFFER_SIZE");

} // namespace Network::Core
