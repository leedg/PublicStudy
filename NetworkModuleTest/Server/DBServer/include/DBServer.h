#pragma once

// English: Database Server main header
// ?쒓?: ?곗씠?곕쿋?댁뒪 ?쒕쾭 硫붿씤 ?ㅻ뜑

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
    // ?쒓?: ?곗씠?곕쿋?댁뒪 ?쒕쾭 ?대옒??
    // =============================================================================
    
    class DBServer
    {
    public:
        // English: Constructor
        // ?쒓?: ?앹꽦??
        DBServer();
        
        // English: Destructor
        // ?쒓?: ?뚮㈇??
        ~DBServer();

        // =====================================================================
        // English: Lifecycle management
        // ?쒓?: ?앸챸二쇨린 愿由?
        // =====================================================================

        /**
         * English: Initialize the database server
         * ?쒓?: ?곗씠?곕쿋?댁뒪 ?쒕쾭 珥덇린??
         * @param port Port number to listen on
         * @param maxConnections Maximum allowed connections
         * @return True if initialization succeeded
         */
        bool Initialize(uint16_t port = 8002, size_t maxConnections = 1000);

        /**
         * English: Start the database server
         * ?쒓?: ?곗씠?곕쿋?댁뒪 ?쒕쾭 ?쒖옉
         * @return True if server started successfully
         */
        bool Start();

        /**
         * English: Stop the database server
         * ?쒓?: ?곗씠?곕쿋?댁뒪 ?쒕쾭 以묒?
         */
        void Stop();

        /**
         * English: Check if server is running
         * ?쒓?: ?쒕쾭 ?ㅽ뻾 ?곹깭 ?뺤씤
         * @return True if server is running
         */
        bool IsRunning() const;

        // =====================================================================
        // English: Configuration
        // ?쒓?: ?ㅼ젙
        // =====================================================================

        /**
         * English: Set database connection parameters
         * ?쒓?: ?곗씠?곕쿋?댁뒪 ?곌껐 ?뚮씪誘명꽣 ?ㅼ젙
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
        // ?쒓?: ?ㅽ듃?뚰겕 ?대깽???몃뱾??
        // =====================================================================

        /**
         * English: Handle new connection
         * ?쒓?: ???곌껐 泥섎━
         * @param connectionId Connection ID
         */
        void OnConnectionEstablished(ConnectionId connectionId);

        /**
         * English: Handle connection closed
         * ?쒓?: ?곌껐 醫낅즺 泥섎━
         * @param connectionId Connection ID
         */
        void OnConnectionClosed(ConnectionId connectionId);

        /**
         * English: Handle data received
         * ?쒓?: ?곗씠???섏떊 泥섎━
         * @param connectionId Connection ID
         * @param data Received data
         * @param size Data size
         */
        void OnDataReceived(ConnectionId connectionId, const uint8_t* data, size_t size);

        /**
         * English: Handle Ping message
         * ?쒓?: Ping 硫붿떆吏 泥섎━
         * @param message Ping message
         */
        void OnPingMessage(const Protocols::Message& message);

        // =====================================================================
        // English: Database operations
        // ?쒓?: ?곗씠?곕쿋?댁뒪 ?묒뾽
        // =====================================================================

        /**
         * English: Connect to database
         * ?쒓?: ?곗씠?곕쿋?댁뒪 ?곌껐
         * @return True if connection succeeded
         */
        bool ConnectToDatabase();

        /**
         * English: Disconnect from database
         * ?쒓?: ?곗씠?곕쿋?댁뒪 ?곌껐 ?댁젣
         */
        void DisconnectFromDatabase();

        /**
         * English: Execute query
         * ?쒓?: 荑쇰━ ?ㅽ뻾
         * @param query SQL query
         * @return Query result
         */
        std::string ExecuteQuery(const std::string& query);

        // =====================================================================
        // English: Private members
        // ?쒓?: 鍮꾧났媛?硫ㅻ쾭
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
        // ?쒓?: 鍮꾧났媛?硫붿냼??
        // =====================================================================

        /**
         * English: Worker thread function
         * ?쒓?: ?뚯빱 ?ㅻ젅???⑥닔
         */
        void WorkerThread();

        /**
         * English: Send message to connection
         * ?쒓?: ?곌껐濡?硫붿떆吏 ?꾩넚
         * @param connectionId Connection ID
         * @param type Message type
         * @param data Message data
         * @param size Data size
         */
        void SendMessage(ConnectionId connectionId, Protocols::MessageType type, 
                       const void* data, size_t size);

        /**
         * English: Get current timestamp
         * ?쒓?: ?꾩옱 ??꾩뒪?ы봽 議고쉶
         * @return Timestamp in milliseconds
         */
        uint64_t GetCurrentTimestamp() const;
    };

} // namespace Network::DBServer
