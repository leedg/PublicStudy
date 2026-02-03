// English: DB processing module implementation
// 한글: DB 처리 모듈 구현

#include "../include/DBProcessingModule.h"
#include <chrono>
#include <ctime>

namespace Network::DBServer
{
DBProcessingModule::DBProcessingModule() = default;

DBProcessingModule::~DBProcessingModule() = default;

void DBProcessingModule::RecordPingPongTimeUtc(uint64_t connectionId,
											   uint64_t pingTimestampMs,
											   uint64_t pongTimestampMs)
{
	const auto pingUtc = FormatUtcTimestamp(pingTimestampMs);
	const auto pongUtc = FormatUtcTimestamp(pongTimestampMs);
	const std::string summary = pingUtc + " / " + pongUtc;

	{
		std::lock_guard<std::mutex> lock(mMutex);
		mLastPingPongTimeUtc[connectionId] = summary;
	}

	// 한글: 실제 DB 저장 호출은 여기에서 연결되며 현재는 비워둔다.
	PersistPingPongTimeUtc(connectionId, summary);
}

std::string
DBProcessingModule::GetLastPingPongTimeUtc(uint64_t connectionId) const
{
	std::lock_guard<std::mutex> lock(mMutex);
	const auto it = mLastPingPongTimeUtc.find(connectionId);
	if (it == mLastPingPongTimeUtc.end())
	{
		return {};
	}
	return it->second;
}

std::string DBProcessingModule::FormatUtcTimestamp(uint64_t timestampMs)
{
	const auto timePoint =
		std::chrono::system_clock::time_point(std::chrono::milliseconds(
			static_cast<int64_t>(timestampMs)));
	const std::time_t timeValue = std::chrono::system_clock::to_time_t(timePoint);

	std::tm utcTime {};
#ifdef _WIN32
	gmtime_s(&utcTime, &timeValue);
#else
	gmtime_r(&timeValue, &utcTime);
#endif

	char buffer[32];
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &utcTime);
	return std::string(buffer);
}

void DBProcessingModule::PersistPingPongTimeUtc(uint64_t connectionId,
												const std::string &gmtTime)
{
	(void)connectionId;
	(void)gmtTime;
	// 한글: 실제 DB 호출은 추후 구현 예정 (현재는 공백 처리).
}

} // namespace Network::DBServer
