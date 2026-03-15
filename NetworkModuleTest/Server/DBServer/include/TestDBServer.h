#pragma once

// TestDBServer main header - database server using NetworkEngine (multi-platform)

// DBPingTimeManager removed — its functionality is now in ServerLatencyManager
#include "ServerLatencyManager.h"
#include "ServerPacketHandler.h"
#include "OrderedTaskQueue.h"
#include "Network/Core/NetworkEngine.h"
#include "Network/Core/Session.h"
#include "Network/Core/SessionManager.h"
#include "Utils/NetworkUtils.h"
#include <memory>
#include <atomic>

namespace Network::DBServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // DBSession - extended session for game server connections
    // =============================================================================

    class DBSession : public Core::Session
    {
    public:
        DBSession();
        virtual ~DBSession();

        // Set packet handler
        void SetPacketHandler(ServerPacketHandler* handler);

        // Session event overrides
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

    private:
        ServerPacketHandler* mPacketHandler;  // Not owned
    };

    using DBSessionRef = std::shared_ptr<DBSession>;

    // =============================================================================
    // TestDBServer class - manages database operations for game servers
    // =============================================================================

    class TestDBServer
    {
    public:
        TestDBServer();
        ~TestDBServer();

        // Lifecycle
        bool Initialize(uint16_t port           = Utils::DEFAULT_TEST_DB_PORT,
                        size_t   dbWorkerCount = Utils::DEFAULT_DB_WORKER_COUNT);
        bool Start();
        void Stop();
        bool IsRunning() const;

    private:
        // Network event handlers
        void OnConnectionEstablished(const Core::NetworkEventData& eventData);
        void OnConnectionClosed(const Core::NetworkEventData& eventData);
        void OnDataReceived(const Core::NetworkEventData& eventData);


    private:
        // Network engine (multi-platform support)
        std::unique_ptr<Core::INetworkEngine>       mEngine;

        // Unified latency manager (handles RTT stats + ping time persistence)
        //          Previously split across ServerLatencyManager + DBPingTimeManager.
        std::unique_ptr<ServerLatencyManager>       mLatencyManager;

        std::unique_ptr<ServerPacketHandler>        mPacketHandler;

        // Ordered task queue for per-serverId ordering guarantee.
        //          Uses hash-based thread affinity: same serverId -> same worker thread.
        //
        //          *** DECLARATION ORDER IS LOAD-BEARING ***
        //          mOrderedTaskQueue MUST be declared AFTER mLatencyManager so that it
        //          is destroyed FIRST (C++ reverses declaration order on destruction).
        //          ~OrderedTaskQueue() calls Shutdown(), which drains all queued lambdas
        //          that hold raw pointers into mLatencyManager. If this order is reversed,
        //          those lambdas would access a destroyed mLatencyManager → use-after-free.
        //
        //
        std::unique_ptr<OrderedTaskQueue>           mOrderedTaskQueue;

        // Server state
        std::atomic<bool>                           mIsRunning;
        uint16_t                                    mPort;
    };

} // namespace Network::DBServer
