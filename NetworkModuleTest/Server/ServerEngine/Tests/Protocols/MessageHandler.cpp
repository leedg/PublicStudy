// English: Implementation of MessageHandler
// 한글: MessageHandler 구현

#include "MessageHandler.h"
// #include "ping.pb.h"  // TODO: Generate Protocol Buffer files
#include <algorithm>

namespace Network::Protocols
{
    // =============================================================================
    // English: Constructor and Destructor
    // 한글: 생성자 및 소멸자
    // =============================================================================
    
    MessageHandler::MessageHandler()
        : mNextMessageId(1)
    {
    }
    
    // =============================================================================
    // English: Handler registration
    // 한글: 핸들러 등록
    // =============================================================================
    
    bool MessageHandler::RegisterHandler(MessageType type, MessageHandlerCallback callback)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        
        mHandlers[type] = callback;
        return true;
    }
    
    void MessageHandler::UnregisterHandler(MessageType type)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        
        mHandlers.erase(type);
    }
    
    // =============================================================================
    // English: Message processing
    // 한글: 메시지 처리
    // =============================================================================
    
    bool MessageHandler::ProcessMessage(
        ConnectionId connectionId,
        const uint8_t* data,
        size_t size
    )
    {
        if (!data || size == 0)
            return false;
            
        MessageType type = GetMessageType(data, size);
        if (type == MessageType::Unknown)
            return false;
            
        Message message;
        message.mType = type;
        message.mConnectionId = connectionId;
        message.mData.assign(data, data + size);
        message.mTimestamp = GetCurrentTimestamp();
        
        std::lock_guard<std::mutex> lock(mMutex);
        
        auto it = mHandlers.find(type);
        if (it != mHandlers.end())
        {
            it->second(message);
            return true;
        }
        
        return false;
    }
    
    std::vector<uint8_t> MessageHandler::CreateMessage(
        MessageType type,
        ConnectionId connectionId,
        const void* data,
        size_t size
    )
    {
        std::vector<uint8_t> message;
        
        // Simple message format: [type(4 bytes)][connection_id(8 bytes)][timestamp(8 bytes)][data]
        
        // Message type
        uint32_t typeValue = static_cast<uint32_t>(type);
        message.insert(message.end(), 
                      reinterpret_cast<const uint8_t*>(&typeValue),
                      reinterpret_cast<const uint8_t*>(&typeValue) + sizeof(typeValue));
        
        // Connection ID
        message.insert(message.end(),
                      reinterpret_cast<const uint8_t*>(&connectionId),
                      reinterpret_cast<const uint8_t*>(&connectionId) + sizeof(connectionId));
        
        // Timestamp
        uint64_t timestamp = GetCurrentTimestamp();
        message.insert(message.end(),
                      reinterpret_cast<const uint8_t*>(&timestamp),
                      reinterpret_cast<const uint8_t*>(&timestamp) + sizeof(timestamp));
        
        // Data
        if (data && size > 0)
        {
            message.insert(message.end(),
                          static_cast<const uint8_t*>(data),
                          static_cast<const uint8_t*>(data) + size);
        }
        
        return message;
    }
    
    // =============================================================================
    // English: Static utility methods
    // 한글: 정적 유틸리티 메소드
    // =============================================================================
    
    MessageType MessageHandler::GetMessageType(const uint8_t* data, size_t size)
    {
        if (!data || size < sizeof(uint32_t))
            return MessageType::Unknown;
            
        uint32_t typeValue;
        std::memcpy(&typeValue, data, sizeof(uint32_t));
        
        if (typeValue == static_cast<uint32_t>(MessageType::Ping) ||
            typeValue == static_cast<uint32_t>(MessageType::Pong) ||
            typeValue >= static_cast<uint32_t>(MessageType::CustomStart))
        {
            return static_cast<MessageType>(typeValue);
        }
        
        return MessageType::Unknown;
    }
    
    bool MessageHandler::ValidateMessage(const uint8_t* data, size_t size)
    {
        if (!data || size < sizeof(uint32_t) + sizeof(ConnectionId) + sizeof(uint64_t))
            return false;
            
        return GetMessageType(data, size) != MessageType::Unknown;
    }
    
    // =============================================================================
    // English: Private helper method
    // 한글: 비공개 헬퍼 메소드
    // =============================================================================
    
    uint64_t MessageHandler::GetCurrentTimestamp() const
    {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }
    
} // namespace Network::Protocols