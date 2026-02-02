#pragma once

// English: Core network abstraction layer for NetworkModule
// 한글: NetworkModule용 핵심 네트워크 추상화 레이어

#include "../../Utils/NetworkUtils.h"
#include "AsyncIOProvider.h"
#include <memory>
#include <functional>

namespace Network::Core
{
    // English: Import utility types into Core namespace
    // 한글: Core 네임스페이스에 유틸리티 타입 가져오기
    using Utils::ConnectionId;
    using Utils::Timestamp;
    // =============================================================================
    // English: Network event types
    // 한글: 네트워크 이벤트 타입
    // =============================================================================
    
    enum class NetworkEvent : uint8_t
    {
        // English: New connection established
        // 한글: 새 연결 수립
        Connected,
        
        // English: Connection closed
        // 한글: 연결 종료
        Disconnected,
        
        // English: Data received
        // 한글: 데이터 수신
        DataReceived,
        
        // English: Data sent successfully
        // 한글: 데이터 전송 성공
        DataSent,
        
        // English: Error occurred
        // 한글: 에러 발생
        Error
    };
    
    // =============================================================================
    // English: Network event data
    // 한글: 네트워크 이벤트 데이터
    // =============================================================================
    
    struct NetworkEventData
    {
        NetworkEvent eventType;
        ConnectionId connectionId;
        size_t dataSize;
        OSError errorCode;
        Timestamp timestamp;
        std::unique_ptr<uint8_t[]> data;
    };
    
    // =============================================================================
    // English: Event callback type
    // 한글: 이벤트 콜백 타입
    // =============================================================================
    
    using NetworkEventCallback = std::function<void(const NetworkEventData&)>;
    
    // =============================================================================
    // English: Core network interface
    // 한글: 핵심 네트워크 인터페이스
    // =============================================================================
    
    class INetworkEngine
    {
    public:
        // English: Virtual destructor
        // 한글: 가상 소멸자
        virtual ~INetworkEngine() = default;
        
        // =====================================================================
        // English: Lifecycle management
        // 한글: 생명주기 관리
        // =====================================================================
        
        /**
         * English: Initialize the network engine
         * 한글: 네트워크 엔진 초기화
         * @param maxConnections Maximum allowed connections
         * @param port Port number to listen on
         * @return True if initialization succeeded
         */
        virtual bool Initialize(size_t maxConnections, uint16_t port) = 0;
        
        /**
         * English: Start the network engine
         * 한글: 네트워크 엔진 시작
         * @return True if started successfully
         */
        virtual bool Start() = 0;
        
        /**
         * English: Stop the network engine
         * 한글: 네트워크 엔진 중지
         */
        virtual void Stop() = 0;
        
        /**
         * English: Check if engine is running
         * 한글: 엔진 실행 상태 확인
         * @return True if running
         */
        virtual bool IsRunning() const = 0;
        
        // =====================================================================
        // English: Event handling
        // 한글: 이벤트 처리
        // =====================================================================
        
        /**
         * English: Register event callback
         * 한글: 이벤트 콜백 등록
         * @param eventType Event type to register for
         * @param callback Callback function
         * @return True if registration succeeded
         */
        virtual bool RegisterEventCallback(NetworkEvent eventType, NetworkEventCallback callback) = 0;
        
        /**
         * English: Unregister event callback
         * 한글: 이벤트 콜백 등록 해제
         * @param eventType Event type
         */
        virtual void UnregisterEventCallback(NetworkEvent eventType) = 0;
        
        // =====================================================================
        // English: Connection management
        // 한글: 연결 관리
        // =====================================================================
        
        /**
         * English: Send data to specific connection
         * 한글: 특정 연결로 데이터 전송
         * @param connectionId Connection ID
         * @param data Data to send
         * @param size Data size
         * @return True if send initiated successfully
         */
        virtual bool SendData(ConnectionId connectionId, const void* data, size_t size) = 0;
        
        /**
         * English: Close specific connection
         * 한글: 특정 연결 종료
         * @param connectionId Connection ID
         */
        virtual void CloseConnection(ConnectionId connectionId) = 0;
        
        /**
         * English: Get connection information
         * 한글: 연결 정보 조회
         * @param connectionId Connection ID
         * @return Connection info or empty if not found
         */
        virtual std::string GetConnectionInfo(ConnectionId connectionId) const = 0;
        
        // =====================================================================
        // English: Statistics
        // 한글: 통계
        // =====================================================================
        
        struct Statistics
        {
            uint64_t totalConnections;
            uint64_t activeConnections;
            uint64_t totalBytesSent;
            uint64_t totalBytesReceived;
            uint64_t totalErrors;
            Timestamp startTime;
        };

        /**
         * English: Get engine statistics
         * 한글: 엔진 통계 조회
         * @return Statistics object
         */
        virtual Statistics GetStatistics() const = 0;
    };
    
    // =============================================================================
    // English: Factory function
    // 한글: 팩토리 함수
    // =============================================================================
    
    /**
     * English: Create network engine instance
     * 한글: 네트워크 엔진 인스턴스 생성
     * @param engineType Engine type (e.g., "asyncio", "epoll", "iocp")
     * @return Network engine instance or nullptr
     */
    std::unique_ptr<INetworkEngine> CreateNetworkEngine(const std::string& engineType = "asyncio");
    
    /**
     * English: Get list of available engine types
     * 한글: 사용 가능한 엔진 타입 목록 조회
     * @return Vector of engine type names
     */
    std::vector<std::string> GetAvailableEngineTypes();
    
} // namespace Network::Core
