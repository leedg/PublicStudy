// English: TestServer implementation with separated handlers
// Korean: 분리된 핸들러를 가진 TestServer 구현

#include "../include/TestServer.h"
#include <iostream>

namespace Network::TestServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    // =============================================================================
    // English: TestServer implementation
    // Korean: TestServer 구현
    // =============================================================================

    TestServer::TestServer()
        : mIsRunning(false)
        , mPort(0)
    {
    }

    TestServer::~TestServer()
    {
        if (mIsRunning.load())
        {
            Stop();
        }
    }

    bool TestServer::Initialize(uint16_t port, const std::string& dbConnectionString)
    {
        mPort = port;
        mDbConnectionString = dbConnectionString;

        // English: Initialize DB packet handler for server-to-server communication
        // Korean: 서버 간 통신을 위한 DB 패킷 핸들러 초기화
        mDBPacketHandler = std::make_unique<DBServerPacketHandler>();

        // English: Create and initialize client network engine
        // Korean: 클라이언트 네트워크 엔진 생성 및 초기화
        mClientEngine = std::make_unique<IOCPNetworkEngine>();

        constexpr size_t MAX_CONNECTIONS = 10000;
        if (!mClientEngine->Initialize(MAX_CONNECTIONS, port))
        {
            Logger::Error("Failed to initialize client network engine");
            return false;
        }

        // English: Register event callbacks for client connections
        // Korean: 클라이언트 연결에 대한 이벤트 콜백 등록
        mClientEngine->RegisterEventCallback(NetworkEvent::Connected,
            [this](const NetworkEventData& e) { OnClientConnectionEstablished(e); });

        mClientEngine->RegisterEventCallback(NetworkEvent::Disconnected,
            [this](const NetworkEventData& e) { OnClientConnectionClosed(e); });

        mClientEngine->RegisterEventCallback(NetworkEvent::DataReceived,
            [this](const NetworkEventData& e) { OnClientDataReceived(e); });

        Logger::Info("TestServer initialized on port " + std::to_string(port));
        return true;
    }

    bool TestServer::Start()
    {
        if (!mClientEngine)
        {
            Logger::Error("TestServer not initialized");
            return false;
        }

        if (!mClientEngine->Start())
        {
            Logger::Error("Failed to start client network engine");
            return false;
        }

        mIsRunning.store(true);
        Logger::Info("TestServer started");
        return true;
    }

    void TestServer::Stop()
    {
        if (!mIsRunning.load())
        {
            return;
        }

        mIsRunning.store(false);

        if (mClientEngine)
        {
            mClientEngine->Stop();
        }

        // English: Disconnect from DB server if connected
        // Korean: DB 서버에 연결되어 있다면 연결 해제
        if (mDBServerSession)
        {
            mDBServerSession->Close();
            mDBServerSession.reset();
        }

        Logger::Info("TestServer stopped");
    }

    bool TestServer::IsRunning() const
    {
        return mIsRunning.load();
    }

    bool TestServer::ConnectToDBServer(const std::string& host, uint16_t port)
    {
        Logger::Info("Connecting to DB server at " + host + ":" + std::to_string(port));

        // English: TODO - Implement client-side connection to DB server
        // Korean: TODO - DB 서버로의 클라이언트 측 연결 구현
        // For now, this is a placeholder
        // In full implementation, you would:
        // 1. Create a client socket
        // 2. Connect to DB server
        // 3. Create a session for the connection
        // 4. Set up packet handler

        Logger::Warn("ConnectToDBServer not yet fully implemented");
        return false;
    }

    void TestServer::OnClientConnectionEstablished(const NetworkEventData& eventData)
    {
        Logger::Info("Client connected - Connection: " + std::to_string(eventData.connectionId));
    }

    void TestServer::OnClientConnectionClosed(const NetworkEventData& eventData)
    {
        Logger::Info("Client disconnected - Connection: " + std::to_string(eventData.connectionId));
    }

    void TestServer::OnClientDataReceived(const NetworkEventData& eventData)
    {
        Logger::Debug("Received " + std::to_string(eventData.dataSize) +
            " bytes from client Connection: " + std::to_string(eventData.connectionId));
    }

    SessionRef TestServer::CreateGameSession()
    {
        return std::make_shared<GameSession>();
    }

} // namespace Network::TestServer
