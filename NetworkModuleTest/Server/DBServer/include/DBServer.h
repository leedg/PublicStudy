#pragma once

// English: Database Server main header
// 한글: 데이터베이스 서버 메인 헤더

#include "../ServerEngine/Core/AsyncIOProvider.h"
#include "../ServerEngine/Protocols/MessageHandler.h"
#include "../ServerEngine/Protocols/PingPong.h"
#include <memory>
#include <thread>
#include <atomic>

namespace Network::DBServer
{
    // =============================================================================
    // English: Database Server class
    // 한글: 데이터베이스 서버 클래스
    // =============================================================================
    
    class DBServer
    {
    public:
        // English: Constructor
        // 한글: 생성자
        DBServer();
        
        // English: Destructor
        // 한글: 소멸자
        ~DBServer();

        // =====================================================================
        // English: Lifecycle management
        // 한글: 생명주기 관리
        // =====================================================================

        /**
         * English: Initialize the database server
         * 한글: 데이터베이스 서버 초기화
         * @param port Port number to listen on
         * @param maxConnections Maximum allowed connections
         * @return True if initialization succeeded
         */
        bool Initialize(uint16_t port = 8002, size_t maxConnections = 1000);

        /**
         * English: Start the database server
         * 한글: 데이터베이스 서버 시작
         * @return True if server started successfully
         */
        bool Start();

        /**
         * English: Stop the database server
         * 한글: 데이터베이스 서버 중지
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
         * English: Set database connection parameters
         * 한글: 데이터베이스 연결 파라미터 설정
         * @param host Database host
         * @param port Database port
         * @param database Database name
         * @param username Username
         * @param password Password
         */
        void SetDatabaseConfig(
            const std::string& host,
            uint16_t port,
            const std::string& database,
            const std::string& username,
            const std::string& password
        );

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

        // =====================================================================
        // English: Database operations
        // 한글: 데이터베이스 작업
        // =====================================================================

        /**
         * English: Connect to database
         * 한글: 데이터베이스 연결
         * @return True if connection succeeded
         */
        bool ConnectToDatabase();

        /**
         * English: Disconnect from database
         * 한글: 데이터베이스 연결 해제
         */
        void DisconnectFromDatabase();

        /**
         * English: Execute query
         * 한글: 쿼리 실행
         * @param query SQL query
         * @return Query result
         */
        std::string ExecuteQuery(const std::string& query);

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

        // Database configuration
        struct DatabaseConfig {
            std::string host = "localhost";
            uint16_t port = 5432;
            std::string database = "networkdb";
            std::string username = "postgres";
            std::string password = "password";
        } mDbConfig;

        // Worker thread
        std::thread mWorkerThread;
        
        // Connection management
        std::unordered_map<ConnectionId, std::string> mConnections;
        std::mutex mConnectionsMutex;

        // =====================================================================
        // English: Private methods
        // 한글: 비공개 메소드
        // =====================================================================

        /**
         * English: Worker thread function
         * 한글: 워커 스레드 함수
         */
        void WorkerThread();

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

} // namespace Network::DBServer