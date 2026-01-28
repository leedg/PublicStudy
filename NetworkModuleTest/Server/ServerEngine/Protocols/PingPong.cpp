// English: Implementation of PingPong handler
// 한글: PingPong 핸들러 구현

#include "Protocols/PingPong.h"
#include "ping.pb.h"
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
    // English: Serialization methods
    // 한글: 직렬화 메소드
    // =============================================================================
    
    std::vector<uint8_t> PingPongHandler::CreatePing(
        const std::string& message,
        uint32_t sequence
    )
    {
        Ping ping;
        
        ping.set_timestamp(GetCurrentTimestamp());
        ping.set_message(message.empty() ? "ping" : message);
        ping.set_sequence(sequence == 0 ? mNextSequence++ : sequence);
        
        std::vector<uint8_t> data;
        data.resize(ping.ByteSizeLong());
        ping.SerializeToArray(data.data(), static_cast<int>(data.size()));
        
        return data;
    }
    
    std::vector<uint8_t> PingPongHandler::CreatePong(
        const std::vector<uint8_t>& pingData,
        const std::string& response
    )
    {
        if (!ParsePing(pingData))
        {
            return {}; // Invalid ping data
        }
        
        Pong pong;
        
        pong.set_timestamp(GetCurrentTimestamp());
        pong.set_message(response.empty() ? "pong" : response);
        pong.set_ping_timestamp(mLastPing->timestamp());
        pong.set_ping_sequence(mLastPing->sequence());
        
        std::vector<uint8_t> data;
        data.resize(pong.ByteSizeLong());
        pong.SerializeToArray(data.data(), static_cast<int>(data.size()));
        
        return data;
    }
    
    // =============================================================================
    // English: Deserialization methods
    // 한글: 역직렬화 메소드
    // =============================================================================
    
    bool PingPongHandler::ParsePing(const std::vector<uint8_t>& data)
    {
        if (data.empty())
            return false;
            
        mLastPing = std::make_unique<Ping>();
        
        if (!mLastPing->ParseFromArray(data.data(), static_cast<int>(data.size())))
        {
            mLastPing.reset();
            return false;
        }
        
        return true;
    }
    
    bool PingPongHandler::ParsePong(const std::vector<uint8_t>& data)
    {
        if (data.empty())
            return false;
            
        mLastPong = std::make_unique<Pong>();
        
        if (!mLastPong->ParseFromArray(data.data(), static_cast<int>(data.size())))
        {
            mLastPong.reset();
            return false;
        }
        
        return true;
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
    // English: Accessors
    // 한글: 접근자
    // =============================================================================
    
    const Ping* PingPongHandler::GetLastPing() const
    {
        return mLastPing.get();
    }
    
    const Pong* PingPongHandler::GetLastPong() const
    {
        return mLastPong.get();
    }
    
} // namespace Network::Protocols