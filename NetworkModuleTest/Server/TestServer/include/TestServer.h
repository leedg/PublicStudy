#pragma once

// TestServer main header - game server using NetworkEngine (multi-platform)

// Forward-declare IDatabase so we can own the local DB without including ServerEngine headers here
namespace Network { namespace Database { class IDatabase; } }

#include "ClientPacketHandler.h"
#include "DBServerSession.h"
#include "DBTaskQueue.h"
#include "Concurrency/TimerQueue.h"
#include "Network/Core/NetworkEngine.h"
#include "Network/Core/SessionManager.h"
#include "Utils/NetworkUtils.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Network::TestServer
{
    using Utils::ConnectionId;

    // =============================================================================
    // TestServer class - manages game clients and DB server connections
    // =============================================================================

    class TestServer
    {
    public:
        TestServer();
        ~TestServer();

        // Lifecycle
        bool Initialize(uint16_t port                  = Utils::DEFAULT_TEST_SERVER_PORT,
                        const std::string& dbConnectionString = "",
                        const std::string& engineType         = "auto",
                        size_t             dbWorkerCount      = Utils::DEFAULT_TASK_QUEUE_WORKER_COUNT);
        bool Start();
        void Stop();
        bool IsRunning() const;

        // Connect to DB server
        bool ConnectToDBServer(const std::string& host, uint16_t port);

    private:
        // Network event handlers for client connections
        void OnClientConnectionEstablished(const Core::NetworkEventData& eventData);
        void OnClientConnectionClosed(const Core::NetworkEventData& eventData);
        void OnClientDataReceived(const Core::NetworkEventData& eventData);

        // DB server helpers
        void DisconnectFromDBServer();
        bool SendDBPacket(const void* data, uint32_t size);
        void DBRecvLoop();
        void SendDBPing();   // Send one DB ping packet (called by timer)
        void DBReconnectLoop();

    private:
        // Packet handler shared across all client sessions (stateless after ctor).
        //          Allocated once in TestServer ctor, injected into per-session OnRecv callbacks.
        std::unique_ptr<ClientPacketHandler>        mPacketHandler;

        // Client connection engine (multi-platform support)
        std::unique_ptr<Core::INetworkEngine>       mClientEngine;

        // DB Server connection (typed session replaces raw Core::SessionRef)
        DBServerSessionRef                           mDBServerSession;

        // Local database owned by TestServer, injected into DBTaskQueue.
        //          MockDatabase if dbConnectionString is empty; SQLiteDatabase otherwise.
        //          Outlives mDBTaskQueue (declared first, destroyed last — reversed destruction order).
        std::unique_ptr<Network::Database::IDatabase> mLocalDatabase;

        // Asynchronous DB task queue — shared_ptr so the session configurator lambda
        //          can capture a weak_ptr.  Session OnRecv callbacks observe the queue via
        //          weak_ptr::lock(); if the queue is destroyed before a late IOCP completion
        //          fires, lock() returns nullptr and the callback skips the enqueue safely (no UAF).
        std::shared_ptr<DBTaskQueue>                mDBTaskQueue;

        // Server state
        std::atomic<bool>                           mIsRunning;
        uint16_t                                    mPort;
        std::string                                 mDbConnectionString;
        std::string                                 mEngineType;

#ifdef _WIN32
        // DB server connection state (Windows-only for now)
        SocketHandle                                mDBServerSocket;
        std::atomic<bool>                           mDBRunning;
        std::atomic<uint32_t>                       mDBPingSequence;
        std::thread                                 mDBRecvThread;
        // mDBPingThread replaced by TimerQueue — mDBPingTimer holds the handle.
        Network::Concurrency::TimerQueue            mTimerQueue;
        Network::Concurrency::TimerQueue::TimerHandle mDBPingTimer{0};
        std::mutex                                  mDBSendMutex;
        // Condition variable to interrupt reconnect loop sleep on shutdown
        std::condition_variable                     mDBShutdownCV;
        std::mutex                                  mDBShutdownMutex;
        std::vector<char>                           mDBRecvBuffer;
        // Read offset for O(1) buffer consumption (avoids O(n) erase)
        size_t                                      mDBRecvOffset = 0;
        // Stored endpoint for DB reconnect
        std::string                                 mDBHost;
        uint16_t                                    mDBPort = 0;
        std::thread                                 mDBReconnectThread;
        std::atomic<bool>                           mDBReconnectRunning;
        // Last WSA error from ConnectToDBServer() — used to distinguish
        //          WSAECONNREFUSED (server shutting down) from other failures
        std::atomic<int>                            mLastDBConnectError{0};
#endif
    };

} // namespace Network::TestServer
