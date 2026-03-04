// English: Implementation of PingPong handler
// 한글: PingPong 핸들러 구현

#include "PingPong.h"

#ifdef HAS_PROTOBUF
#include "ping.pb.h"
#endif

#include <chrono>
#include <cstring>
#include <random>
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
// English: Write a variable-length validation array: [uint8_t count][T × count]
// 한글: 가변 길이 검증 배열 직렬화: [uint8_t count][T × count]
template <typename T>
void WriteValidationArray(std::vector<uint8_t> &buffer, const std::vector<T> &arr)
{
	WriteScalar(buffer, static_cast<uint8_t>(arr.size()));
	for (const T &v : arr)
		WriteScalar(buffer, v);
}

template <typename T>
bool ReadValidationArray(const std::vector<uint8_t> &buffer, size_t &offset,
                         std::vector<T> &out)
{
	uint8_t count = 0;
	if (!ReadScalar(buffer, offset, count))
		return false;
	out.resize(count);
	for (uint8_t i = 0; i < count; ++i)
		if (!ReadScalar(buffer, offset, out[i]))
			return false;
	return true;
}

// English: Generate 1–5 random uint32 numbers and 1–5 random printable ASCII chars.
// 한글: 랜덤 uint32 숫자 1~5개 및 출력 가능한 ASCII 문자 1~5개 생성.
inline void GenerateValidationPayload(std::vector<uint32_t> &nums,
                                      std::vector<char> &chars)
{
	static thread_local std::mt19937 rng{std::random_device{}()};
	std::uniform_int_distribution<int>      countDist(1, 5);
	std::uniform_int_distribution<uint32_t> numDist(0, UINT32_MAX);
	// Printable ASCII: 0x21('!') – 0x7E('~')
	std::uniform_int_distribution<int>      charDist(0x21, 0x7E);

	const int numCount  = countDist(rng);
	const int charCount = countDist(rng);

	nums.resize(numCount);
	for (int i = 0; i < numCount; ++i)
		nums[i] = numDist(rng);

	chars.resize(charCount);
	for (int i = 0; i < charCount; ++i)
		chars[i] = static_cast<char>(charDist(rng));
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

	GenerateValidationPayload(mLastPingValidationNums, mLastPingValidationChars);
	mLastValidationOk = false;

	std::vector<uint8_t> data;
	WriteScalar(data, timestamp);
	WriteScalar(data, actualSequence);
	WriteString(data, actualMessage);
	WriteValidationArray(data, mLastPingValidationNums);
	WriteValidationArray(data, mLastPingValidationChars);

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
	WriteScalar(data, timestamp);
	WriteScalar(data, mLastPingTimestamp);
	WriteScalar(data, mLastPingSequence);
	WriteString(data, actualResponse);
	// Echo the validation payload parsed from the ping back to the sender.
	// 수신한 ping의 검증 페이로드를 그대로 에코 반환.
	WriteValidationArray(data, mLastPingValidationNums);
	WriteValidationArray(data, mLastPingValidationChars);

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
	// Read and store validation payload so CreatePong can echo it back.
	// 검증 페이로드를 읽어 저장 — CreatePong이 에코할 때 사용.
	if (!ReadValidationArray(data, offset, mLastPingValidationNums))
		return false;
	if (!ReadValidationArray(data, offset, mLastPingValidationChars))
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

	// Read echoed validation payload and verify against the original sent in CreatePing.
	// 에코된 검증 페이로드를 읽어 CreatePing에서 송신한 원본과 대조.
	std::vector<uint32_t> echoNums;
	std::vector<char>     echoChars;
	if (!ReadValidationArray(data, offset, echoNums))
		return false;
	if (!ReadValidationArray(data, offset, echoChars))
		return false;
	mLastValidationOk = (echoNums == mLastPingValidationNums) &&
	                    (echoChars == mLastPingValidationChars);

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
