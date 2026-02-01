// English: Header file for PingPong handler
// ?쒓?: PingPong ?몃뱾???ㅻ뜑 ?뚯씪

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
    // ?쒓?: PingPong ?몃뱾???대옒??
    // =============================================================================

    class PingPongHandler
    {
    public:
        // English: Constructor and Destructor
        // ?쒓?: ?앹꽦??諛??뚮㈇??
        PingPongHandler();
        ~PingPongHandler();

        // English: Serialization methods
        // ?쒓?: 吏곷젹??硫붿냼??
        std::vector<uint8_t> CreatePing(
            const std::string& message = "",
            uint32_t sequence = 0
        );

        std::vector<uint8_t> CreatePong(
            const std::vector<uint8_t>& pingData,
            const std::string& response = ""
        );

        // English: Deserialization methods
        // ?쒓?: ??쭅?ы솕 硫붿냼??
        bool ParsePing(const std::vector<uint8_t>& data);
        bool ParsePong(const std::vector<uint8_t>& data);

        // English: Utility methods
        // ?쒓?: ?좏떥由ы떚 硫붿냼??
        uint64_t CalculateRTT(
            uint64_t pingTimestamp,
            uint64_t pongTimestamp
        ) const;

        uint64_t GetCurrentTimestamp() const;

#ifdef HAS_PROTOBUF
        // English: Accessors (available only with protobuf)
        // ?쒓?: ?묎렐??(protobuf ?덉쓣 ?뚮쭔 ?ъ슜 媛??
        const ping::Ping* GetLastPing() const;
        const ping::Pong* GetLastPong() const;
#endif

    private:
        // English: Member variables
        // ?쒓?: 硫ㅻ쾭 蹂??
        uint32_t mNextSequence;

#ifdef HAS_PROTOBUF
        std::unique_ptr<ping::Ping> mLastPing;
        std::unique_ptr<ping::Pong> mLastPong;
#endif
    };

} // namespace Network::Protocols

