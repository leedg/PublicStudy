// English: LatencyStats implementation
// 한글: LatencyStats 구현

#include "../include/LatencyStats.h"
#include <climits>

namespace Network::TestClient
{

LatencyStats::LatencyStats() { Reset(); }

void LatencyStats::Update(uint64_t rtt)
{
	lastRtt = rtt;
	if (rtt < minRtt)
		minRtt = rtt;
	if (rtt > maxRtt)
		maxRtt = rtt;

	// English: Running average calculation
	// 한글: 이동 평균 계산
	avgRtt =
		((avgRtt * pongCount) + static_cast<double>(rtt)) / (pongCount + 1);
	++pongCount;
}

void LatencyStats::Reset()
{
	lastRtt = 0;
	minRtt = UINT64_MAX;
	maxRtt = 0;
	avgRtt = 0.0;
	pingCount = 0;
	pongCount = 0;
}

} // namespace Network::TestClient
