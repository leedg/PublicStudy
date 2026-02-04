// English: TestDBServer implementation
// Korean: TestDBServer 구현

#include "../include/TestDBServer.h"
#include <iostream>

namespace Network::DBServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    // =============================================================================
    // English: DBSession implementation
    // Korean: DBSession 구현
    // =============================================================================

    DBSession::DBSession()
        : mPacketHandler(nullptr)
    {
    }

    DBSession::~DBSession()
    {
    }

    void DBSession::SetPacketHandler(ServerPacketHandler* handler)
    {
        mPacketHandler = handler;
    }

    void DBSession::OnConnected()
    {
        Logger::Info("DBSession connected - ID: " + std::to_string(GetId()));
    }

    void DBSession::OnDisconnected()
    {
        Logger::Info("DBSession disconnected - ID: " + std::to_string(GetId()));
    }

    void DBSession::OnRecv(const char* data, uint32_t size)
    {
        if (mPacketHandler)
        {
            mPacketHandler->ProcessPacket(this, data, size);
        }
    }

    // =============================================================================
    // English: TestDBServer implementation
    // Korean: TestDBServer 구현
    // =============================================================================

    TestDBServer::TestDBServer()
        : mIsRunning(false)
        , mPort(0)
    {
    }

    TestDBServer::~TestDBServer()
    {
        if (mIsRunning.load())
        {
            Stop();
        }
    }

    bool TestDBServer::Initialize(uint16_t port)
    {
        mPort = port;

        // English: Initialize DB ping time manager
        // Korean: DB ping 시간 관리자 초기화
        mDBPingTimeManager = std::make_unique<DBPingTimeManager>();
        if (!mDBPingTimeManager->Initialize())
        {
            Logger::Error("Failed to initialize DB ping time manager");
            return false;
        }

        // English: Initialize packet handler
        // Korean: 패킷 핸들러 초기화
        mPacketHandler = std::make_unique<ServerPacketHandler>();
        mPacketHandler->Initialize(mDBPingTimeManager.get());

        // English: Create and initialize network engine
        // Korean: 네트워크 엔진 생성 및 초기화
        mEngine = std::make_unique<IOCPNetworkEngine>();

        constexpr size_t MAX_CONNECTIONS = 1000;
        if (!mEngine->Initialize(MAX_CONNECTIONS, port))
        {
            Logger::Error("Failed to initialize network engine");
            return false;
        }

        // English: Register event callbacks
        // Korean: 이벤트 콜백 등록
        mEngine->RegisterEventCallback(NetworkEvent::Connected,
            [this](const NetworkEventData& e) { OnConnectionEstablished(e); });

        mEngine->RegisterEventCallback(NetworkEvent::Disconnected,
            [this](const NetworkEventData& e) { OnConnectionClosed(e); });

        mEngine->RegisterEventCallback(NetworkEvent::DataReceived,
            [this](const NetworkEventData& e) { OnDataReceived(e); });

        Logger::Info("TestDBServer initialized on port " + std::to_string(port));
        return true;
    }

    bool TestDBServer::Start()
    {
        if (!mEngine)
        {
            Logger::Error("TestDBServer not initialized");
            return false;
        }

        if (!mEngine->Start())
        {
            Logger::Error("Failed to start network engine");
            return false;
        }

        mIsRunning.store(true);
        Logger::Info("TestDBServer started");
        return true;
    }

    void TestDBServer::Stop()
    {
        if (!mIsRunning.load())
        {
            return;
        }

        mIsRunning.store(false);

        if (mEngine)
        {
            mEngine->Stop();
        }

        if (mDBPingTimeManager)
        {
            mDBPingTimeManager->Shutdown();
        }

        Logger::Info("TestDBServer stopped");
    }

    bool TestDBServer::IsRunning() const
    {
        return mIsRunning.load();
    }

    void TestDBServer::OnConnectionEstablished(const NetworkEventData& eventData)
    {
        Logger::Info("Game server connected - Connection: " + std::to_string(eventData.connectionId));
    }

    void TestDBServer::OnConnectionClosed(const NetworkEventData& eventData)
    {
        Logger::Info("Game server disconnected - Connection: " + std::to_string(eventData.connectionId));
    }

    void TestDBServer::OnDataReceived(const NetworkEventData& eventData)
    {
        Logger::Debug("Received " + std::to_string(eventData.dataSize) +
            " bytes from Connection: " + std::to_string(eventData.connectionId));
    }

    SessionRef TestDBServer::CreateDBSession()
    {
        return std::make_shared<DBSession>();
    }

} // namespace Network::DBServer
