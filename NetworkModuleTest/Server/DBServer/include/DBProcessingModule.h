#pragma once

// English: DB processing module for TestDBServer
// 한글: TestDBServer용 DB 처리 모듈

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Network::DBServer
{
class DBProcessingModule
{
  public:
	DBProcessingModule();
	~DBProcessingModule();

	// English: Record last ping/pong time in UTC (GMT) format
	// 한글: 마지막 Ping/Pong 시간을 UTC(GMT) 포맷으로 기록
	void RecordPingPongTimeUtc(uint64_t connectionId, uint64_t pingTimestampMs,
							   uint64_t pongTimestampMs);

	// English: Get last stored UTC time string
	// 한글: 마지막 저장된 UTC 시간 문자열 조회
	std::string GetLastPingPongTimeUtc(uint64_t connectionId) const;

  private:
	static std::string FormatUtcTimestamp(uint64_t timestampMs);
	void PersistPingPongTimeUtc(uint64_t connectionId,
								const std::string &gmtTime);

  private:
	mutable std::mutex mMutex;
	std::unordered_map<uint64_t, std::string> mLastPingPongTimeUtc;
};

} // namespace Network::DBServer
