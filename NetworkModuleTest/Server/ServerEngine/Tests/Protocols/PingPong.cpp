// English: Implementation of PingPong handler
// 한글: PingPong 핸들러 구현

#include "PingPong.h"

#ifdef HAS_PROTOBUF
#include "ping.pb.h"
#endif

#include <chrono>
#include <cstring>
#include <sstream>

#ifndef HAS_PROTOBUF
namespace
{
template <typename T>
void WriteScalar(std::vector<uint8_t> &buffer, const T &value)
{
	const auto *ptr = reinterpret_cast<const uint8_t *>(&value);
	buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
}

template <typename T>
bool ReadScalar(const std::vector<uint8_t> &buffer, size_t &offset, T &out)
{
	if (offset + sizeof(T) > buffer.size())
	{
		return false;
	}

	std::memcpy(&out, buffer.data() + offset, sizeof(T));
	offset += sizeof(T);
	return true;
}

bool WriteString(std::vector<uint8_t> &buffer, const std::string &value)
{
	const auto length = static_cast<uint32_t>(value.size());
	WriteScalar(buffer, length);
	if (length == 0)
	{
		return true;
	}
	buffer.insert(buffer.end(), value.begin(), value.end());
	return true;
}

bool ReadString(const std::vector<uint8_t> &buffer, size_t &offset,
				std::string &out)
{
	uint32_t length = 0;
	if (!ReadScalar(buffer, offset, length))
	{
		return false;
	}

	if (offset + length > buffer.size())
	{
		return false;
	}

	out.assign(reinterpret_cast<const char *>(buffer.data() + offset), length);
	offset += length;
	return true;
}
} // namespace
#endif

namespace Network::Protocols
{
// =============================================================================
// English: Constructor and Destructor
// 한글: 생성자 및 소멸자
// =============================================================================

PingPongHandler::PingPongHandler()
	: mNextSequence(1), mLastPingTimestamp(0), mLastPingSequence(0),
	  mLastPingMessage(), mLastPongTimestamp(0), mLastPongPingTimestamp(0),
	  mLastPongPingSequence(0), mLastPongMessage(), mHasLastPing(false),
	  mHasLastPong(false)
{
}

PingPongHandler::~PingPongHandler() = default;

// =============================================================================
// English: Serialization methods (protobuf optional)
// 한글: 직렬화 메소드 (protobuf 선택)
// =============================================================================

std::vector<uint8_t> PingPongHandler::CreatePing(const std::string &message,
												 uint32_t sequence)
{
#ifdef HAS_PROTOBUF
	ping::Ping ping;
	const auto timestamp = GetCurrentTimestamp();
	const auto actualMessage = message.empty() ? "ping" : message;
	const auto actualSequence = sequence == 0 ? mNextSequence++ : sequence;
	ping.set_timestamp(timestamp);
	ping.set_message(actualMessage);
	ping.set_sequence(actualSequence);

	std::vector<uint8_t> data;
	data.resize(ping.ByteSizeLong());
	ping.SerializeToArray(data.data(), static_cast<int>(data.size()));

	// 한글: protobuf 사용 시에도 마지막 값을 보관해서 공통 접근을 지원한다.
	mLastPingTimestamp = timestamp;
	mLastPingSequence = actualSequence;
	mLastPingMessage = actualMessage;
	mHasLastPing = true;
	return data;
#else
	const auto timestamp = GetCurrentTimestamp();
	const auto actualMessage = message.empty() ? "ping" : message;
	const auto actualSequence = sequence == 0 ? mNextSequence++ : sequence;

	std::vector<uint8_t> data;
	data.reserve(sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t) +
				 actualMessage.size());
	WriteScalar(data, timestamp);
	WriteScalar(data, actualSequence);
	WriteString(data, actualMessage);

	mLastPingTimestamp = timestamp;
	mLastPingSequence = actualSequence;
	mLastPingMessage = actualMessage;
	mHasLastPing = true;
	return data;
#endif
}

std::vector<uint8_t>
PingPongHandler::CreatePong(const std::vector<uint8_t> &pingData,
							const std::string &response)
{
#ifdef HAS_PROTOBUF
	if (!ParsePing(pingData))
		return {};

	ping::Pong pong;
	const auto timestamp = GetCurrentTimestamp();
	const auto actualResponse = response.empty() ? "pong" : response;
	pong.set_timestamp(timestamp);
	pong.set_message(actualResponse);
	pong.set_ping_timestamp(mLastPing->timestamp());
	pong.set_ping_sequence(mLastPing->sequence());

	std::vector<uint8_t> data;
	data.resize(pong.ByteSizeLong());
	pong.SerializeToArray(data.data(), static_cast<int>(data.size()));

	mLastPongTimestamp = timestamp;
	mLastPongPingTimestamp = mLastPingTimestamp;
	mLastPongPingSequence = mLastPingSequence;
	mLastPongMessage = actualResponse;
	mHasLastPong = true;
	return data;
#else
	if (!ParsePing(pingData))
		return {};

	const auto timestamp = GetCurrentTimestamp();
	const auto actualResponse = response.empty() ? "pong" : response;

	std::vector<uint8_t> data;
	data.reserve(sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint32_t) +
				 sizeof(uint32_t) + actualResponse.size());
	WriteScalar(data, timestamp);
	WriteScalar(data, mLastPingTimestamp);
	WriteScalar(data, mLastPingSequence);
	WriteString(data, actualResponse);

	mLastPongTimestamp = timestamp;
	mLastPongPingTimestamp = mLastPingTimestamp;
	mLastPongPingSequence = mLastPingSequence;
	mLastPongMessage = actualResponse;
	mHasLastPong = true;
	return data;
#endif
}

// =============================================================================
// English: Deserialization methods
// 한글: 역직렬화 메소드
// =============================================================================

bool PingPongHandler::ParsePing(const std::vector<uint8_t> &data)
{
	mHasLastPing = false;
#ifdef HAS_PROTOBUF
	if (data.empty())
		return false;

	mLastPing = std::make_unique<ping::Ping>();
	if (!mLastPing->ParseFromArray(data.data(), static_cast<int>(data.size())))
	{
		mLastPing.reset();
		return false;
	}
	mLastPingTimestamp = mLastPing->timestamp();
	mLastPingSequence = mLastPing->sequence();
	mLastPingMessage = mLastPing->message();
	mHasLastPing = true;
	return true;
#else
	if (data.empty())
		return false;

	size_t offset = 0;
	uint64_t timestamp = 0;
	uint32_t sequence = 0;
	std::string message;

	if (!ReadScalar(data, offset, timestamp))
		return false;
	if (!ReadScalar(data, offset, sequence))
		return false;
	if (!ReadString(data, offset, message))
		return false;

	mLastPingTimestamp = timestamp;
	mLastPingSequence = sequence;
	mLastPingMessage = message;
	mHasLastPing = true;
	return true;
#endif
}

bool PingPongHandler::ParsePong(const std::vector<uint8_t> &data)
{
	mHasLastPong = false;
#ifdef HAS_PROTOBUF
	if (data.empty())
		return false;

	mLastPong = std::make_unique<ping::Pong>();
	if (!mLastPong->ParseFromArray(data.data(), static_cast<int>(data.size())))
	{
		mLastPong.reset();
		return false;
	}
	mLastPongTimestamp = mLastPong->timestamp();
	mLastPongPingTimestamp = mLastPong->ping_timestamp();
	mLastPongPingSequence = mLastPong->ping_sequence();
	mLastPongMessage = mLastPong->message();
	mHasLastPong = true;
	return true;
#else
	if (data.empty())
		return false;

	size_t offset = 0;
	uint64_t timestamp = 0;
	uint64_t pingTimestamp = 0;
	uint32_t pingSequence = 0;
	std::string message;

	if (!ReadScalar(data, offset, timestamp))
		return false;
	if (!ReadScalar(data, offset, pingTimestamp))
		return false;
	if (!ReadScalar(data, offset, pingSequence))
		return false;
	if (!ReadString(data, offset, message))
		return false;

	mLastPongTimestamp = timestamp;
	mLastPongPingTimestamp = pingTimestamp;
	mLastPongPingSequence = pingSequence;
	mLastPongMessage = message;
	mHasLastPong = true;
	return true;
#endif
}

// =============================================================================
// English: Utility methods
// 한글: 유틸리티 메소드
// =============================================================================

uint64_t PingPongHandler::CalculateRTT(uint64_t pingTimestamp,
										   uint64_t pongTimestamp) const
{
	return pongTimestamp - pingTimestamp;
}

uint64_t PingPongHandler::GetCurrentTimestamp() const
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
		.count();
}

uint64_t PingPongHandler::GetLastPingTimestamp() const
{
	return mHasLastPing ? mLastPingTimestamp : 0;
}

uint32_t PingPongHandler::GetLastPingSequence() const
{
	return mHasLastPing ? mLastPingSequence : 0;
}

uint64_t PingPongHandler::GetLastPongTimestamp() const
{
	return mHasLastPong ? mLastPongTimestamp : 0;
}

uint64_t PingPongHandler::GetLastPongPingTimestamp() const
{
	return mHasLastPong ? mLastPongPingTimestamp : 0;
}

uint32_t PingPongHandler::GetLastPongPingSequence() const
{
	return mHasLastPong ? mLastPongPingSequence : 0;
}

// =============================================================================
// English: Accessors (protobuf only)
// 한글: 접근자 (protobuf 전용)
// =============================================================================

#ifdef HAS_PROTOBUF
const ping::Ping *PingPongHandler::GetLastPing() const
{
	return mLastPing.get();
}

const ping::Pong *PingPongHandler::GetLastPong() const
{
	return mLastPong.get();
}
#endif

} // namespace Network::Protocols
