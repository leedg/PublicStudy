// TestDBServer 구현

#include "../include/TestDBServer.h"
#include <iostream>

namespace Network::DBServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    // =============================================================================
    // DBSession 구현
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
    // TestDBServer 구현
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

        // 통합 레이턴시 관리자 초기화.
        // RTT 통계와 핑 시간 저장을 모두 담당 —
        // 이전의 DBPingTimeManager가 이 클래스에 통합됨.
        mLatencyManager = std::make_unique<ServerLatencyManager>();
        if (!mLatencyManager->Initialize())
        {
            Logger::Error("Failed to initialize server latency manager");
            return false;
        }

        // 설정된 워커 수로 순서 보장 작업 큐 초기화.
        // 서버별 순서 보장을 위해 serverId 기반 해시 친화도 사용.
        // CLI(-w 플래그)로 재설정 가능; 기본값 = DEFAULT_DB_WORKER_COUNT.
        mOrderedTaskQueue = std::make_unique<OrderedTaskQueue>();
        if (!mOrderedTaskQueue->Initialize(dbWorkerCount))
        {
            Logger::Error("Failed to initialize ordered task queue");
            return false;
        }

        // 패킷 핸들러 초기화 — 이제 의존성이 두 개 (DBPingTimeManager 통합)
        mPacketHandler = std::make_unique<ServerPacketHandler>();
        mPacketHandler->Initialize(mLatencyManager.get(),
                                   mOrderedTaskQueue.get());

        // SetSessionConfigurator로 세션별 recv 콜백 등록. 제거된 SessionFactory 패턴 대체.
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

        // 팩토리를 사용하여 네트워크 엔진 생성 및 초기화 (최적 백엔드 자동 감지)
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

        // 이벤트 콜백 등록
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

        // 새로운 연결 수락을 먼저 중지
        if (mEngine)
        {
            mEngine->Stop();
        }

        // 순서 보장 작업 큐 종료 (남은 작업을 처리한 후 중지)
        if (mOrderedTaskQueue)
        {
            Logger::Info("Shutting down ordered task queue...");
            mOrderedTaskQueue->Shutdown();
            Logger::Info("OrderedTaskQueue statistics - Enqueued: " +
                        std::to_string(mOrderedTaskQueue->GetTotalEnqueuedCount()) +
                        ", Processed: " + std::to_string(mOrderedTaskQueue->GetTotalProcessedCount()));
        }

        // 통합 레이턴시 관리자 종료 (RTT 통계와 핑 시간 모두 포함)
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
