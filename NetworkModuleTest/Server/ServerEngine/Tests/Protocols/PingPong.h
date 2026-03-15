#pragma once

// Header file for PingPong handler

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
// PingPong handler class
// =============================================================================

class PingPongHandler
{
  public:
	// Constructor and Destructor
	PingPongHandler();
	~PingPongHandler();

	// Serialization methods
	std::vector<uint8_t> CreatePing(const std::string &message = "",
									uint32_t sequence = 0);

	std::vector<uint8_t> CreatePong(const std::vector<uint8_t> &pingData,
									const std::string &response = "");

	// Deserialization methods
	bool ParsePing(const std::vector<uint8_t> &data);
	bool ParsePong(const std::vector<uint8_t> &data);

	// Utility methods
	uint64_t CalculateRTT(uint64_t pingTimestamp, uint64_t pongTimestamp) const;

	uint64_t GetCurrentTimestamp() const;

	// Last parsed values (works with or without protobuf)
	uint64_t GetLastPingTimestamp() const;
	uint32_t GetLastPingSequence() const;
	uint64_t GetLastPongTimestamp() const;
	uint64_t GetLastPongPingTimestamp() const;
	uint32_t GetLastPongPingSequence() const;

	// Validation payload accessors — available after ParsePong()
	bool                        GetLastValidationResult() const { return mLastValidationOk; }
	const std::vector<uint32_t>& GetLastValidationNums()  const { return mLastPingValidationNums; }
	const std::vector<char>&     GetLastValidationChars()  const { return mLastPingValidationChars; }

#ifdef HAS_PROTOBUF
	// Accessors (available only with protobuf)
	const ping::Ping *GetLastPing() const;
	const ping::Pong *GetLastPong() const;
#endif

  private:
	// Member variables
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

	// Validation payload — sent in Ping, echoed by Pong, verified in ParsePong
	std::vector<uint32_t> mLastPingValidationNums;
	std::vector<char>     mLastPingValidationChars;
	bool                  mLastValidationOk = false;

#ifdef HAS_PROTOBUF
	std::unique_ptr<ping::Ping> mLastPing;
	std::unique_ptr<ping::Pong> mLastPong;
#endif
};

} // namespace Network::Protocols
