#pragma once

// English: Latency statistics tracker and connection state definitions
// 한글: 지연 시간 통계 추적기 및 연결 상태 정의

#include <cstdint>

namespace Network::TestClient
{

// =============================================================================
// English: Latency statistics tracker
// 한글: 지연 시간 통계 추적기
// =============================================================================

struct LatencyStats
{
	uint64_t lastRtt; // English: Last round-trip time (ms) / 한글: 마지막 왕복
					  // 시간 (ms)
	uint64_t minRtt;    // English: Minimum RTT / 한글: 최소 RTT
	uint64_t maxRtt;    // English: Maximum RTT / 한글: 최대 RTT
	double avgRtt;      // English: Average RTT / 한글: 평균 RTT
	uint64_t pingCount; // English: Total pings sent / 한글: 총 핑 전송 수
	uint64_t pongCount; // English: Total pongs received / 한글: 총 퐁 수신 수

	LatencyStats();
	void Update(uint64_t rtt);
	void Reset();
};

// =============================================================================
// English: Connection state enumeration
// 한글: 연결 상태 열거형
// =============================================================================

enum class ClientState : uint8_t
{
	Disconnected = 0, // English: Not connected / 한글: 연결 안됨
	Connecting, // English: TCP connect in progress / 한글: TCP 연결 진행 중
	Connected, // English: TCP connected, handshake pending / 한글: TCP 연결됨,
			   // 핸드셰이크 대기
	SessionActive, // English: Session established / 한글: 세션 수립됨
	Disconnecting, // English: Graceful shutdown in progress / 한글: 정상 종료
				   // 진행 중
};

} // namespace Network::TestClient
