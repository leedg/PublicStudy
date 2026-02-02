// English: Implementation of PingPong handler
// 한글: PingPong 핸들러 구현

#include "PingPong.h"

#ifdef HAS_PROTOBUF
#include "ping.pb.h"
#endif

#include <chrono>
#include <sstream>

namespace Network::Protocols
{
    // =============================================================================
    // English: Constructor and Destructor
    // 한글: 생성자 및 소멸자
    // =============================================================================

    PingPongHandler::PingPongHandler()
        : mNextSequence(1)
    {
    }

    PingPongHandler::~PingPongHandler() = default;

    // =============================================================================
    // English: Serialization methods (requires HAS_PROTOBUF)
    // 한글: 직렬화 메소드 (HAS_PROTOBUF 필요)
    // =============================================================================

    std::vector<uint8_t> PingPongHandler::CreatePing(
        const std::string& message,
        uint32_t sequence
    )
    {
#ifdef HAS_PROTOBUF
        ping::Ping ping;
        ping.set_timestamp(GetCurrentTimestamp());
        ping.set_message(message.empty() ? "ping" : message);
        ping.set_sequence(sequence == 0 ? mNextSequence++ : sequence);

        std::vector<uint8_t> data;
        data.resize(ping.ByteSizeLong());
        ping.SerializeToArray(data.data(), static_cast<int>(data.size()));
        return data;
#else
        (void)message;
        (void)sequence;
        return {};
#endif
    }

    std::vector<uint8_t> PingPongHandler::CreatePong(
        const std::vector<uint8_t>& pingData,
        const std::string& response
    )
    {
#ifdef HAS_PROTOBUF
        if (!ParsePing(pingData))
            return {};

        ping::Pong pong;
        pong.set_timestamp(GetCurrentTimestamp());
        pong.set_message(response.empty() ? "pong" : response);
        pong.set_ping_timestamp(mLastPing->timestamp());
        pong.set_ping_sequence(mLastPing->sequence());

        std::vector<uint8_t> data;
        data.resize(pong.ByteSizeLong());
        pong.SerializeToArray(data.data(), static_cast<int>(data.size()));
        return data;
#else
        (void)pingData;
        (void)response;
        return {};
#endif
    }

    // =============================================================================
    // English: Deserialization methods
    // 한글: 역직렬화 메소드
    // =============================================================================

    bool PingPongHandler::ParsePing(const std::vector<uint8_t>& data)
    {
#ifdef HAS_PROTOBUF
        if (data.empty())
            return false;

        mLastPing = std::make_unique<ping::Ping>();
        if (!mLastPing->ParseFromArray(data.data(), static_cast<int>(data.size())))
        {
            mLastPing.reset();
            return false;
        }
        return true;
#else
        (void)data;
        return false;
#endif
    }

    bool PingPongHandler::ParsePong(const std::vector<uint8_t>& data)
    {
#ifdef HAS_PROTOBUF
        if (data.empty())
            return false;

        mLastPong = std::make_unique<ping::Pong>();
        if (!mLastPong->ParseFromArray(data.data(), static_cast<int>(data.size())))
        {
            mLastPong.reset();
            return false;
        }
        return true;
#else
        (void)data;
        return false;
#endif
    }

    // =============================================================================
    // English: Utility methods
    // 한글: 유틸리티 메소드
    // =============================================================================

    uint64_t PingPongHandler::CalculateRTT(
        uint64_t pingTimestamp,
        uint64_t pongTimestamp
    ) const
    {
        return pongTimestamp - pingTimestamp;
    }

    uint64_t PingPongHandler::GetCurrentTimestamp() const
    {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    // =============================================================================
    // English: Accessors (protobuf only)
    // 한글: 접근자 (protobuf 전용)
    // =============================================================================

#ifdef HAS_PROTOBUF
    const ping::Ping* PingPongHandler::GetLastPing() const
    {
        return mLastPing.get();
    }

    const ping::Pong* PingPongHandler::GetLastPong() const
    {
        return mLastPong.get();
    }
#endif

} // namespace Network::Protocols
