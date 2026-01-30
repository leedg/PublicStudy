// English: Header file for PingPong handler
// 한글: PingPong 핸들러 헤더 파일

#pragma once

#include <vector>
#include <cstdint>
#include <memory>
#include <string>

#ifdef HAS_PROTOBUF
// Forward declarations for protobuf messages
namespace ping {
    class Ping;
    class Pong;
}
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
        std::vector<uint8_t> CreatePing(
            const std::string& message = "",
            uint32_t sequence = 0
        );

        std::vector<uint8_t> CreatePong(
            const std::vector<uint8_t>& pingData,
            const std::string& response = ""
        );

        // English: Deserialization methods
        // 한글: 역직렬화 메소드
        bool ParsePing(const std::vector<uint8_t>& data);
        bool ParsePong(const std::vector<uint8_t>& data);

        // English: Utility methods
        // 한글: 유틸리티 메소드
        uint64_t CalculateRTT(
            uint64_t pingTimestamp,
            uint64_t pongTimestamp
        ) const;

        uint64_t GetCurrentTimestamp() const;

#ifdef HAS_PROTOBUF
        // English: Accessors (available only with protobuf)
        // 한글: 접근자 (protobuf 있을 때만 사용 가능)
        const ping::Ping* GetLastPing() const;
        const ping::Pong* GetLastPong() const;
#endif

    private:
        // English: Member variables
        // 한글: 멤버 변수
        uint32_t mNextSequence;

#ifdef HAS_PROTOBUF
        std::unique_ptr<ping::Ping> mLastPing;
        std::unique_ptr<ping::Pong> mLastPong;
#endif
    };

} // namespace Network::Protocols
