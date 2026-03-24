#pragma once

// PingPong 핸들러 — Ping/Pong 패킷 직렬화·역직렬화 및 RTT 계산.
//
// protobuf 사용 여부에 따라 빌드가 분기된다:
//   - HAS_PROTOBUF 정의 시: ping.pb.h 기반 직렬화
//   - 미정의 시: 단순 바이너리 포맷 (WriteScalar/ReadScalar)
//
// 검증 페이로드:
//   Ping에 랜덤 uint32 배열 + ASCII 문자 배열을 포함시키고,
//   Pong이 에코한 값을 ParsePong()에서 원본과 대조하여 데이터 정합성을 검증한다.

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

class PingPongHandler
{
  public:
	PingPongHandler();
	~PingPongHandler();

	// Ping/Pong 패킷 생성 (직렬화)
	std::vector<uint8_t> CreatePing(const std::string &message = "",
									uint32_t sequence = 0);

	std::vector<uint8_t> CreatePong(const std::vector<uint8_t> &pingData,
									const std::string &response = "");

	// Ping/Pong 패킷 파싱 (역직렬화)
	bool ParsePing(const std::vector<uint8_t> &data);
	bool ParsePong(const std::vector<uint8_t> &data);

	// RTT 계산 (밀리초)
	uint64_t CalculateRTT(uint64_t pingTimestamp, uint64_t pongTimestamp) const;

	uint64_t GetCurrentTimestamp() const;

	// 마지막으로 파싱된 값 — ParsePing()/ParsePong() 호출 후 유효 (protobuf 유무 무관)
	uint64_t GetLastPingTimestamp() const;
	uint32_t GetLastPingSequence() const;
	uint64_t GetLastPongTimestamp() const;
	uint64_t GetLastPongPingTimestamp() const;
	uint32_t GetLastPongPingSequence() const;

	// 검증 페이로드 접근자 — ParsePong() 호출 후 유효
	bool                        GetLastValidationResult() const { return mLastValidationOk; }
	const std::vector<uint32_t>& GetLastValidationNums()  const { return mLastPingValidationNums; }
	const std::vector<char>&     GetLastValidationChars()  const { return mLastPingValidationChars; }

#ifdef HAS_PROTOBUF
	const ping::Ping *GetLastPing() const;
	const ping::Pong *GetLastPong() const;
#endif

  private:
	// ── Ping 상태 ───────────────────────────────────────────────────────────
	uint32_t    mNextSequence;          // CreatePing()에서 자동 증가하는 시퀀스 번호 (1부터 시작)
	uint64_t    mLastPingTimestamp;     // 마지막으로 생성/파싱한 Ping 타임스탬프 (밀리초)
	uint32_t    mLastPingSequence;      // 마지막으로 생성/파싱한 Ping 시퀀스 번호
	std::string mLastPingMessage;       // 마지막으로 생성/파싱한 Ping 메시지 문자열
	bool        mHasLastPing;           // ParsePing() 또는 CreatePing() 성공 후 true

	// ── Pong 상태 ───────────────────────────────────────────────────────────
	uint64_t    mLastPongTimestamp;     // 마지막으로 생성/파싱한 Pong 타임스탬프 (밀리초)
	uint64_t    mLastPongPingTimestamp; // Pong에 에코된 원본 Ping 타임스탬프 (RTT 계산 기준)
	uint32_t    mLastPongPingSequence;  // Pong에 에코된 원본 Ping 시퀀스 번호
	std::string mLastPongMessage;       // 마지막으로 생성/파싱한 Pong 메시지 문자열
	bool        mHasLastPong;           // ParsePong() 또는 CreatePong() 성공 후 true

	// ── 검증 페이로드: Ping에 포함 → Pong이 에코 → ParsePong에서 원본 대조 ──
	std::vector<uint32_t> mLastPingValidationNums;  // CreatePing()에서 생성된 1~5개 랜덤 숫자
	std::vector<char>     mLastPingValidationChars; // CreatePing()에서 생성된 1~5개 랜덤 ASCII 문자
	bool                  mLastValidationOk = false; // ParsePong()에서 에코값과 원본 대조 결과

#ifdef HAS_PROTOBUF
	std::unique_ptr<ping::Ping> mLastPing; // protobuf 모드: ParsePing() 후 저장되는 Ping 메시지 객체
	std::unique_ptr<ping::Pong> mLastPong; // protobuf 모드: ParsePong() 후 저장되는 Pong 메시지 객체
#endif
};

} // namespace Network::Protocols
