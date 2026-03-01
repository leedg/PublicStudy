#pragma once

// English: Header file for PingPong handler
// 한글: PingPong 핸들러 헤더 파일

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef HAS_PROTOBUF
// Forward declarations for protobuf messages
namespace ping
{
class Ping;
class Pong;
} // namespace ping
#endif

namespace Network::Protocols
{
// =============================================================================
// English: PingPong handler class
// 한글: PingPong 핸들러 클래스
// =============================================================================

class PingPongHandler
{
  public:
	// English: Constructor and Destructor
	// 한글: 생성자 및 소멸자
	PingPongHandler();
	~PingPongHandler();

	// English: Serialization methods
	// 한글: 직렬화 메소드
	std::vector<uint8_t> CreatePing(const std::string &message = "",
									uint32_t sequence = 0);

	std::vector<uint8_t> CreatePong(const std::vector<uint8_t> &pingData,
									const std::string &response = "");

	// English: Deserialization methods
	// 한글: 역직렬화 메소드
	bool ParsePing(const std::vector<uint8_t> &data);
	bool ParsePong(const std::vector<uint8_t> &data);

	// English: Utility methods
	// 한글: 유틸리티 메소드
	uint64_t CalculateRTT(uint64_t pingTimestamp, uint64_t pongTimestamp) const;

	uint64_t GetCurrentTimestamp() const;

	// English: Last parsed values (works with or without protobuf)
	// 한글: 마지막 파싱 값 (protobuf 유무와 무관)
	uint64_t GetLastPingTimestamp() const;
	uint32_t GetLastPingSequence() const;
	uint64_t GetLastPongTimestamp() const;
	uint64_t GetLastPongPingTimestamp() const;
	uint32_t GetLastPongPingSequence() const;

	// English: Validation payload accessors — available after ParsePong()
	// 한글: 검증 페이로드 접근자 — ParsePong() 호출 후 유효
	bool                        GetLastValidationResult() const { return mLastValidationOk; }
	const std::vector<uint32_t>& GetLastValidationNums()  const { return mLastPingValidationNums; }
	const std::vector<char>&     GetLastValidationChars()  const { return mLastPingValidationChars; }

#ifdef HAS_PROTOBUF
	// English: Accessors (available only with protobuf)
	// 한글: 접근자 (protobuf 있을 때만 사용 가능)
	const ping::Ping *GetLastPing() const;
	const ping::Pong *GetLastPong() const;
#endif

  private:
	// English: Member variables
	// 한글: 멤버 변수
	uint32_t mNextSequence;
	uint64_t mLastPingTimestamp;
	uint32_t mLastPingSequence;
	std::string mLastPingMessage;
	uint64_t mLastPongTimestamp;
	uint64_t mLastPongPingTimestamp;
	uint32_t mLastPongPingSequence;
	std::string mLastPongMessage;
	bool mHasLastPing;
	bool mHasLastPong;

	// English: Validation payload — sent in Ping, echoed by Pong, verified in ParsePong
	// 한글: 검증 페이로드 — Ping에 포함해 송신, Pong이 에코, ParsePong에서 원본 대조
	std::vector<uint32_t> mLastPingValidationNums;  // 1~5개 랜덤 숫자
	std::vector<char>     mLastPingValidationChars; // 1~5개 랜덤 문자
	bool                  mLastValidationOk = false;

#ifdef HAS_PROTOBUF
	std::unique_ptr<ping::Ping> mLastPing;
	std::unique_ptr<ping::Pong> mLastPong;
#endif
};

} // namespace Network::Protocols
