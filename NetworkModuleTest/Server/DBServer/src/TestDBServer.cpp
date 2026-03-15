// TestDBServer implementation

#include "../include/TestDBServer.h"
#include <iostream>

namespace Network::DBServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    // =============================================================================
    // DBSession implementation
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
    // TestDBServer implementation
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

    bool TestDBServer::Initialize(uint16_t port, size_t dbWorkerCount)
    {
        mPort = port;

        // Initialize unified latency manager.
        //          Handles both RTT statistics and ping time persistence —
        //          the former DBPingTimeManager is now merged into this class.
        mLatencyManager = std::make_unique<ServerLatencyManager>();
        if (!mLatencyManager->Initialize())
        {
            Logger::Error("Failed to initialize server latency manager");
            return false;
        }

        // Initialize ordered task queue with the configured worker count.
        //          Uses serverId-based hash affinity for per-server ordering guarantee.
        //          Configurable via CLI (-w flag); default = DEFAULT_DB_WORKER_COUNT.
        mOrderedTaskQueue = std::make_unique<OrderedTaskQueue>();
        if (!mOrderedTaskQueue->Initialize(dbWorkerCount))
        {
            Logger::Error("Failed to initialize ordered task queue");
            return false;
        }

        // Initialize packet handler — only two dependencies now (DBPingTimeManager merged)
        mPacketHandler = std::make_unique<ServerPacketHandler>();
        mPacketHandler->Initialize(mLatencyManager.get(),
                                   mOrderedTaskQueue.get());

        // Register per-session recv callback via SetSessionConfigurator.
        //          Replaces the removed SessionFactory pattern.
        {
            ServerPacketHandler* handlerPtr = mPacketHandler.get();
            Core::SessionManager::Instance().SetSessionConfigurator(
                [handlerPtr](Core::Session* session)
                {
                    session->SetOnRecv(
                        [handlerPtr](Core::Session* s, const char* data, uint32_t size)
                        {
                            handlerPtr->ProcessPacket(s, data, size);
                        });
                });
        }

        // Create and initialize network engine using factory (auto-detect best backend)
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

        // Register event callbacks
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

        // Stop accepting new connections first
        if (mEngine)
        {
            mEngine->Stop();
        }

        // Shutdown ordered task queue (drains remaining tasks before stopping)
        if (mOrderedTaskQueue)
        {
            Logger::Info("Shutting down ordered task queue...");
            mOrderedTaskQueue->Shutdown();
            Logger::Info("OrderedTaskQueue statistics - Enqueued: " +
                        std::to_string(mOrderedTaskQueue->GetTotalEnqueuedCount()) +
                        ", Processed: " + std::to_string(mOrderedTaskQueue->GetTotalProcessedCount()));
        }

        // Shutdown unified latency manager (covers both RTT stats and ping time)
        if (mLatencyManager)
        {
            mLatencyManager->Shutdown();
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

} // namespace Network::DBServer
