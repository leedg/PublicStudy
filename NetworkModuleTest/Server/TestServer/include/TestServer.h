#pragma once

// English: TestServer main header
// 한글: TestServer 메인 헤더

#include "../ServerEngine/Core/AsyncIOProvider.h"
#include "../ServerEngine/Protocols/MessageHandler.h"
#include "../ServerEngine/Protocols/PingPong.h"
#include <memory>
#include <thread>
#include <atomic>

namespace Network::TestServer
{
    // =============================================================================
    // English: Client information structure
    // 한글: 클라이언트 정보 구조체
    // =============================================================================
    
    struct ClientInfo
    {
        ConnectionId connectionId;
        uint64_t connectedAt;
        bool isAuthenticated;
        std::string userId;
    };
    
    // =============================================================================
    // English: DBServer configuration
    // 한글: DBServer 설정
    // =============================================================================
    
    struct DBServerConfig
    {
        std::string host = "localhost";
        uint16_t port = 8002;
    };

    // =============================================================================
    // English: TestServer class
    // 한글: TestServer 클래스
    // =============================================================================
    
    class TestServer
    {
    public:
        // English: Constructor
        // 한글: 생성자
        TestServer();
        
        // English: Destructor
        // 한글: 소멸자
        ~TestServer();

        // =====================================================================
        // English: Lifecycle management
        // 한글: 생명주기 관리
        // =====================================================================

        /**
         * English: Initialize the test server
         * 한글: 테스트 서버 초기화
         * @param port Port number to listen on
         * @param maxConnections Maximum allowed connections
         * @return True if initialization succeeded
         */
        bool Initialize(uint16_t port = 8001, size_t maxConnections = 10000);

        /**
         * English: Start the test server
         * 한글: 테스트 서버 시작
         * @return True if server started successfully
         */
        bool Start();

        /**
         * English: Stop the test server
         * 한글: 테스트 서버 중지
         */
        void Stop();

        /**
         * English: Check if server is running
         * 한글: 서버 실행 상태 확인
         * @return True if server is running
         */
        bool IsRunning() const;

        // =====================================================================
        // English: Configuration
        // 한글: 설정
        // =====================================================================

        /**
         * English: Set DBServer configuration
         * 한글: DBServer 설정
         * @param host DBServer host
         * @param port DBServer port
         */
        void SetDBServerConfig(const std::string& host, uint16_t port);

    private:
        // =====================================================================
        // English: Network event handlers
        // 한글: 네트워크 이벤트 핸들러
        // =====================================================================

        /**
         * English: Handle new connection
         * 한글: 새 연결 처리
         * @param connectionId Connection ID
         */
        void OnConnectionEstablished(ConnectionId connectionId);

        /**
         * English: Handle connection closed
         * 한글: 연결 종료 처리
         * @param connectionId Connection ID
         */
        void OnConnectionClosed(ConnectionId connectionId);

        /**
         * English: Handle data received
         * 한글: 데이터 수신 처리
         * @param connectionId Connection ID
         * @param data Received data
         * @param size Data size
         */
        void OnDataReceived(ConnectionId connectionId, const uint8_t* data, size_t size);

        /**
         * English: Handle Ping message
         * 한글: Ping 메시지 처리
         * @param message Ping message
         */
        void OnPingMessage(const Protocols::Message& message);

        /**
         * English: Handle Pong message
         * 한글: Pong 메시지 처리
         * @param message Pong message
         */
        void OnPongMessage(const Protocols::Message& message);

        // =====================================================================
        // English: DBServer communication
        // 한글: DBServer 통신
        // =====================================================================

        /**
         * English: Connect to DBServer
         * 한글: DBServer에 연결
         * @return True if connection succeeded
         */
        bool ConnectToDBServer();

        /**
         * English: Disconnect from DBServer
         * 한글: DBServer 연결 해제
         */
        void DisconnectFromDBServer();

        /**
         * English: Send message to DBServer
         * 한글: DBServer로 메시지 전송
         * @param type Message type
         * @param data Message data
         * @param size Data size
         */
        void SendToDBServer(Protocols::MessageType type, const void* data, size_t size);

        // =====================================================================
        // English: Business logic
        // 한글: 비즈니스 로직
        // =====================================================================

        /**
         * English: Process client request
         * 한글: 클라이언트 요청 처리
         * @param connectionId Connection ID
         * @param request Request data
         */
        void ProcessClientRequest(ConnectionId connectionId, const std::string& request);

        // =====================================================================
        // English: Private members
        // 한글: 비공개 멤버
        // =====================================================================

        // Network components
        std::unique_ptr<AsyncIO::AsyncIOProvider> mAsyncIOProvider;
        std::unique_ptr<Protocols::MessageHandler> mMessageHandler;
        std::unique_ptr<Protocols::PingPongHandler> mPingPongHandler;

        // Server state
        std::atomic<bool> mIsRunning;
        std::atomic<bool> mIsInitialized;
        uint16_t mPort;
        size_t mMaxConnections;

        // DBServer connection
        DBServerConfig mDbServerConfig;
        std::atomic<bool> mDbServerConnected;

        // Worker threads
        std::thread mNetworkThread;
        std::thread mLogicThread;
        
        // Connection management
        std::unordered_map<ConnectionId, ClientInfo> mConnections;
        std::mutex mConnectionsMutex;

        // =====================================================================
        // English: Private methods
        // 한글: 비공개 메소드
        // =====================================================================

        /**
         * English: Network worker thread function
         * 한글: 네트워크 워커 스레드 함수
         */
        void NetworkWorkerThread();

        /**
         * English: Logic worker thread function
         * 한글: 로직 워커 스레드 함수
         */
        void LogicWorkerThread();

        /**
         * English: Send message to connection
         * 한글: 연결로 메시지 전송
         * @param connectionId Connection ID
         * @param type Message type
         * @param data Message data
         * @param size Data size
         */
        void SendMessage(ConnectionId connectionId, Protocols::MessageType type, 
                       const void* data, size_t size);

        /**
         * English: Get current timestamp
         * 한글: 현재 타임스탬프 조회
         * @return Timestamp in milliseconds
         */
        uint64_t GetCurrentTimestamp() const;
    };

} // namespace Network::TestServer