// English: TestServer main implementation
// 한글: TestServer 메인 구현

#include "include/TestServer.h"
#include <iostream>
#include <chrono>
#include <cstring>

using namespace Network::AsyncIO;
using namespace Network::Protocols;

namespace Network::TestServer
{
    // =============================================================================
    // English: Constructor and Destructor
    // 한글: 생성자 및 소멸자
    // =============================================================================
    
    TestServer::TestServer()
        : mIsRunning(false)
        , mIsInitialized(false)
        , mPort(8001)
        , mMaxConnections(10000)
    {
    }
    
    TestServer::~TestServer()
    {
        if (mIsRunning)
        {
            Stop();
        }
    }
    
    // =============================================================================
    // English: Lifecycle management
    // 한글: 생명주기 관리
    // =============================================================================
    
    bool TestServer::Initialize(uint16_t port, size_t maxConnections)
    {
        if (mIsInitialized)
        {
            std::cerr << "TestServer already initialized" << std::endl;
            return false;
        }
        
        mPort = port;
        mMaxConnections = maxConnections;
        
        // Create AsyncIO provider
        mAsyncIOProvider = CreateAsyncIOProvider();
        if (!mAsyncIOProvider)
        {
            std::cerr << "Failed to create AsyncIO provider" << std::endl;
            return false;
        }
        
        // Initialize AsyncIO provider
        auto error = mAsyncIOProvider->Initialize(1024, maxConnections);
        if (error != AsyncIOError::Success)
        {
            std::cerr << "Failed to initialize AsyncIO provider: " 
                      << static_cast<int>(error) << std::endl;
            return false;
        }
        
        // Create message handler
        mMessageHandler = std::make_unique<MessageHandler>();
        mPingPongHandler = std::make_unique<PingPongHandler>();
        
        // Register message handlers
        mMessageHandler->RegisterHandler(
            MessageType::Ping,
            [this](const Message& msg) { OnPingMessage(msg); }
        );
        
        mMessageHandler->RegisterHandler(
            MessageType::Pong,
            [this](const Message& msg) { OnPongMessage(msg); }
        );
        
        mIsInitialized = true;
        std::cout << "TestServer initialized on port " << port << std::endl;
        return true;
    }
    
    bool TestServer::Start()
    {
        if (!mIsInitialized)
        {
            std::cerr << "TestServer not initialized" << std::endl;
            return false;
        }
        
        if (mIsRunning)
        {
            std::cerr << "TestServer already running" << std::endl;
            return false;
        }
        
        // Connect to DBServer
        if (!ConnectToDBServer())
        {
            std::cerr << "Failed to connect to DBServer" << std::endl;
            return false;
        }
        
        mIsRunning = true;
        
        // Start worker threads
        mNetworkThread = std::thread(&TestServer::NetworkWorkerThread, this);
        mLogicThread = std::thread(&TestServer::LogicWorkerThread, this);
        
        std::cout << "TestServer started successfully" << std::endl;
        return true;
    }
    
    void TestServer::Stop()
    {
        if (!mIsRunning)
            return;
            
        mIsRunning = false;
        
        // Wait for worker threads to finish
        if (mNetworkThread.joinable())
        {
            mNetworkThread.join();
        }
        
        if (mLogicThread.joinable())
        {
            mLogicThread.join();
        }
        
        // Disconnect from DBServer
        DisconnectFromDBServer();
        
        // Shutdown AsyncIO provider
        if (mAsyncIOProvider)
        {
            mAsyncIOProvider->Shutdown();
        }
        
        std::cout << "TestServer stopped" << std::endl;
    }
    
    bool TestServer::IsRunning() const
    {
        return mIsRunning;
    }
    
    void TestServer::SetDBServerConfig(const std::string& host, uint16_t port)
    {
        mDbServerConfig.host = host;
        mDbServerConfig.port = port;
    }
    
    // =============================================================================
    // English: Network event handlers
    // 한글: 네트워크 이벤트 핸들러
    // =============================================================================
    
    void TestServer::OnConnectionEstablished(ConnectionId connectionId)
    {
        std::lock_guard<std::mutex> lock(mConnectionsMutex);
        ClientInfo clientInfo;
        clientInfo.connectionId = connectionId;
        clientInfo.connectedAt = GetCurrentTimestamp();
        clientInfo.isAuthenticated = false;
        mConnections[connectionId] = clientInfo;
        
        std::cout << "New client connection: " << connectionId << std::endl;
        
        // Send welcome message (ping)
        auto pingData = mPingPongHandler->CreatePing(
            "Welcome to TestServer!", 
            static_cast<uint32_t>(connectionId)
        );
        
        SendMessage(connectionId, MessageType::Ping, 
                   pingData.data(), pingData.size());
    }
    
    void TestServer::OnConnectionClosed(ConnectionId connectionId)
    {
        std::lock_guard<std::mutex> lock(mConnectionsMutex);
        mConnections.erase(connectionId);
        
        std::cout << "Client connection closed: " << connectionId << std::endl;
    }
    
    void TestServer::OnDataReceived(ConnectionId connectionId, const uint8_t* data, size_t size)
    {
        if (!data || size == 0)
            return;
            
        // Process message through message handler
        mMessageHandler->ProcessMessage(connectionId, data, size);
    }
    
    void TestServer::OnPingMessage(const Message& message)
    {
        // Parse ping message
        if (!mPingPongHandler->ParsePing(message.mData))
        {
            std::cerr << "Invalid ping message received" << std::endl;
            return;
        }
        
        auto ping = mPingPongHandler->GetLastPing();
        std::cout << "Ping received from client " << message.mConnectionId
                  << ": " << ping->message() << " (seq: " << ping->sequence() << ")" << std::endl;
        
        // Create pong response
        auto pongData = mPingPongHandler->CreatePong(
            message.mData, 
            "TestServer Pong Response"
        );
        
        if (!pongData.empty())
        {
            SendMessage(message.mConnectionId, MessageType::Pong, 
                       pongData.data(), pongData.size());
        }
    }
    
    void TestServer::OnPongMessage(const Message& message)
    {
        // Parse pong message
        if (!mPingPongHandler->ParsePong(message.mData))
        {
            std::cerr << "Invalid pong message received" << std::endl;
            return;
        }
        
        auto pong = mPingPongHandler->GetLastPong();
        
        // Calculate RTT if we have the original ping timestamp
        uint64_t rtt = mPingPongHandler->CalculateRTT(
            pong->ping_timestamp(), 
            pong->timestamp()
        );
        
        std::cout << "Pong received from client " << message.mConnectionId
                  << " - RTT: " << rtt << "ms" << std::endl;
    }
    
    // =============================================================================
    // English: DBServer communication
    // 한글: DBServer 통신
    // =============================================================================
    
    bool TestServer::ConnectToDBServer()
    {
        std::cout << "Connecting to DBServer at " 
                  << mDbServerConfig.host << ":" << mDbServerConfig.port << std::endl;
        
        // For now, simulate successful connection
        // In real implementation, establish actual network connection
        mDbServerConnected = true;
        
        // Send initial ping to DBServer
        auto pingData = mPingPongHandler->CreatePing(
            "TestServer initialization", 0
        );
        
        SendToDBServer(MessageType::Ping, pingData.data(), pingData.size());
        
        return true;
    }
    
    void TestServer::DisconnectFromDBServer()
    {
        std::cout << "Disconnecting from DBServer" << std::endl;
        mDbServerConnected = false;
        // In real implementation, close network connection
    }
    
    void TestServer::SendToDBServer(MessageType type, const void* data, size_t size)
    {
        if (!mDbServerConnected)
        {
            std::cerr << "Not connected to DBServer" << std::endl;
            return;
        }
        
        std::cout << "Sending message type " << static_cast<uint32_t>(type)
                  << " to DBServer" << std::endl;
        
        // For now, just log
        // In real implementation, send through network connection
    }
    
    // =============================================================================
    // English: Business logic
    // 한글: 비즈니스 로직
    // =============================================================================
    
    void TestServer::ProcessClientRequest(ConnectionId connectionId, const std::string& request)
    {
        std::cout << "Processing client request from " << connectionId 
                  << ": " << request << std::endl;
        
        // For now, forward to DBServer
        SendToDBServer(MessageType::CustomStart, request.c_str(), request.length());
    }
    
    // =============================================================================
    // English: Private methods
    // 한글: 비공개 메소드
    // =============================================================================
    
    void TestServer::NetworkWorkerThread()
    {
        std::cout << "TestServer network worker thread started" << std::endl;
        
        while (mIsRunning)
        {
            // Process network events
            const int MAX_EVENTS = 64;
            AsyncIO::CompletionEntry entries[MAX_EVENTS];
            
            int numEvents = mAsyncIOProvider->ProcessCompletions(
                entries, MAX_EVENTS, 100  // 100ms timeout
            );
            
            if (numEvents > 0)
            {
                for (int i = 0; i < numEvents; ++i)
                {
                    const auto& entry = entries[i];
                    
                    // Handle different completion types
                    switch (entry.mType)
                    {
                        case AsyncIO::AsyncIOType::Accept:
                            OnConnectionEstablished(entry.mContext);
                            break;
                            
                        case AsyncIO::AsyncIOType::Recv:
                            // Handle received data
                            std::cout << "Received " << entry.mResult 
                                      << " bytes from connection " 
                                      << entry.mContext << std::endl;
                            // For now, just log
                            break;
                            
                        case AsyncIO::AsyncIOType::Send:
                            // Send completed
                            break;
                            
                        default:
                            break;
                    }
                }
            }
            
            // Small sleep to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        std::cout << "TestServer network worker thread stopped" << std::endl;
    }
    
    void TestServer::LogicWorkerThread()
    {
        std::cout << "TestServer logic worker thread started" << std::endl;
        
        while (mIsRunning)
        {
            // Process business logic
            // For now, just periodic status check
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // Send periodic ping to DBServer
            if (mDbServerConnected)
            {
                auto pingData = mPingPongHandler->CreatePing(
                    "TestServer status check", 0
                );
                SendToDBServer(MessageType::Ping, pingData.data(), pingData.size());
            }
        }
        
        std::cout << "TestServer logic worker thread stopped" << std::endl;
    }
    
    void TestServer::SendMessage(ConnectionId connectionId, MessageType type, 
                               const void* data, size_t size)
    {
        auto message = mMessageHandler->CreateMessage(type, connectionId, data, size);
        
        std::cout << "Sending message type " << static_cast<uint32_t>(type)
                  << " to client " << connectionId << std::endl;
        
        // For now, just log
        // In real implementation, send through AsyncIO provider
    }
    
    uint64_t TestServer::GetCurrentTimestamp() const
    {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }
    
} // namespace Network::TestServer