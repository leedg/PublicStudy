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

        // English: Initialize per-server latency manager
        // Korean: 서버별 레이턴시 관리자 초기화
        mLatencyManager = std::make_unique<ServerLatencyManager>();
        if (!mLatencyManager->Initialize())
        {
            Logger::Error("Failed to initialize server latency manager");
            return false;
        }

        // English: Initialize ordered task queue with 4 worker threads
        //          Uses serverId-based hash affinity for per-server ordering guarantee
        // Korean: 4개 워커 스레드로 순서 보장 작업 큐 초기화
        //         서버별 순서 보장을 위해 serverId 기반 해시 친화도 사용
        mOrderedTaskQueue = std::make_unique<OrderedTaskQueue>();
        if (!mOrderedTaskQueue->Initialize(4))
        {
            Logger::Error("Failed to initialize ordered task queue");
            return false;
        }

        // English: Initialize packet handler with all dependencies
        // Korean: 모든 의존성을 주입하여 패킷 핸들러 초기화
        mPacketHandler = std::make_unique<ServerPacketHandler>();
        mPacketHandler->Initialize(mDBPingTimeManager.get(),
                                    mLatencyManager.get(),
                                    mOrderedTaskQueue.get());

        // English: Set session factory for DB server connections
        // Korean: DB 서버 연결용 세션 팩토리 설정
        Core::SessionManager::Instance().Initialize([this]() {
            auto session = std::make_shared<DBSession>();
            session->SetPacketHandler(mPacketHandler.get());
            return session;
        });

        // English: Create and initialize network engine using factory (auto-detect best backend)
        // Korean: 팩토리를 사용하여 네트워크 엔진 생성 및 초기화 (최적 백엔드 자동 감지)
        mEngine = CreateNetworkEngine("auto");
        if (!mEngine)
        {
            Logger::Error("Failed to create network engine");
            return false;
        }

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

        // English: Stop accepting new connections first
        // Korean: 새로운 연결 수락을 먼저 중지
        if (mEngine)
        {
            mEngine->Stop();
        }

        // English: Shutdown ordered task queue (drains remaining tasks before stopping)
        // Korean: 순서 보장 작업 큐 종료 (남은 작업을 처리한 후 중지)
        if (mOrderedTaskQueue)
        {
            Logger::Info("Shutting down ordered task queue...");
            mOrderedTaskQueue->Shutdown();
            Logger::Info("OrderedTaskQueue statistics - Enqueued: " +
                        std::to_string(mOrderedTaskQueue->GetTotalEnqueuedCount()) +
                        ", Processed: " + std::to_string(mOrderedTaskQueue->GetTotalProcessedCount()));
        }

        // English: Shutdown latency manager
        // Korean: 레이턴시 관리자 종료
        if (mLatencyManager)
        {
            mLatencyManager->Shutdown();
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
