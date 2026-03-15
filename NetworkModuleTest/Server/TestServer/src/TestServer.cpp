// TestServer implementation with separated handlers

#include "../include/TestServer.h"
#include "Network/Core/ServerPacketDefine.h"
// Full IDatabase + DatabaseFactory definitions needed to create the local database instance
#include "Interfaces/IDatabase.h"
#include "Database/DatabaseFactory.h"
#include <mutex>
#include <thread>
#include <chrono>
#include "Utils/StringUtil.h"
#include <ctime>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

namespace Network::TestServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    // =============================================================================
    // TestServer implementation
    // =============================================================================

    TestServer::TestServer()
        : mPacketHandler(std::make_unique<ClientPacketHandler>())
        , mIsRunning(false)
        , mPort(0)
#ifdef _WIN32
        , mDBServerSocket(INVALID_SOCKET)
        , mDBRunning(false)
        , mDBPingSequence(0)
        , mDBReconnectRunning(false)
#endif
    {
#ifdef _WIN32
        mDBRecvBuffer.reserve(8192 * 4);
#endif
    }

    // =============================================================================
    // DB ping helper (called by timer, replaces DBPingLoop thread)
    // =============================================================================

    void TestServer::SendDBPing()
    {
#ifdef _WIN32
        constexpr uint32_t kSaveInterval = 5;

        Core::PKT_ServerPingReq pingPacket;
        pingPacket.sequence  = ++mDBPingSequence;
        pingPacket.timestamp = Timer::GetCurrentTimestamp();
        SendDBPacket(&pingPacket, sizeof(pingPacket));

        if (mDBPingSequence.load() % kSaveInterval == 0)
        {
            Core::PKT_DBSavePingTimeReq savePacket;
            savePacket.serverId  = 1;
            savePacket.timestamp = Timer::GetCurrentTimestamp();
            Network::Utils::StringUtil::Copy(savePacket.serverName, "TestServer");
            SendDBPacket(&savePacket, sizeof(savePacket));
        }
#endif
    }

    TestServer::~TestServer()
    {
        if (mIsRunning.load())
        {
            Stop();
        }
    }

    bool TestServer::Initialize(uint16_t port,
                                const std::string& dbConnectionString,
                                const std::string& engineType,
                                size_t             dbWorkerCount)
    {
        mPort = port;
        mDbConnectionString = dbConnectionString;
        mEngineType = engineType.empty() ? "auto" : engineType;

        // Initialize asynchronous DB task queue FIRST (needed by session factory).
        //          DBTaskQueue routes each task by sessionId % workerCount, so all tasks
        //          for the same session always land on the same worker (FIFO within worker).
        //          Default = DEFAULT_TASK_QUEUE_WORKER_COUNT (1); configurable via CLI -w.
        //          1 worker: simplest deployment, no affinity math needed.
        //          N workers: higher throughput; per-session ordering still guaranteed by hash affinity.
        // Create local database — SQLite when a path is given, Mock otherwise.
        //          The owned instance is injected into DBTaskQueue so it can persist
        //          connect/disconnect/player-data records without a separate DB server.
        {
            using namespace Network::Database;
            if (mDbConnectionString.empty())
            {
                mLocalDatabase = DatabaseFactory::CreateMockDatabase();
                DatabaseConfig cfg;
                cfg.mType = DatabaseType::Mock;
                mLocalDatabase->Connect(cfg);
                Logger::Info("TestServer: using MockDatabase for DBTaskQueue");
            }
            else
            {
                mLocalDatabase = DatabaseFactory::CreateSQLiteDatabase();
                DatabaseConfig cfg;
                cfg.mType = DatabaseType::SQLite;
                cfg.mConnectionString = mDbConnectionString;
                mLocalDatabase->Connect(cfg);
                Logger::Info("TestServer: using SQLiteDatabase (" + mDbConnectionString + ") for DBTaskQueue");
            }
        }

        mDBTaskQueue = std::make_shared<DBTaskQueue>();
        if (!mDBTaskQueue->Initialize(dbWorkerCount, "db_tasks.wal", mLocalDatabase.get()))
        {
            Logger::Error("Failed to initialize DB task queue");
            return false;
        }

#ifdef _WIN32
        // Initialize DB ping timer queue (one background thread, starts here).
        if (!mTimerQueue.Initialize())
        {
            Logger::Error("TestServer: TimerQueue initialization failed");
            return false;
        }
#endif

        // Register per-session recv callback via SessionConfigurator.
        //          Called inside CreateSession before PostRecv() so the first recv
        //          completion is guaranteed to see the callback (no race).
        //          ClientPacketHandler is shared (stateless after ctor) and thread-safe.
        {
            ClientPacketHandler* handlerPtr = mPacketHandler.get();
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

        // Create and initialize client network engine using selected backend.
        //          "auto" keeps platform default selection behavior.
        mClientEngine = CreateNetworkEngine(mEngineType);
        if (!mClientEngine)
        {
            Logger::Error("Failed to create network engine: " + mEngineType);
            return false;
        }

        constexpr size_t kMaxConnections = Utils::MAX_CONNECTIONS;
        if (!mClientEngine->Initialize(kMaxConnections, port))
        {
            Logger::Error("Failed to initialize client network engine");
            return false;
        }

        // Register event callbacks for client connections
        mClientEngine->RegisterEventCallback(NetworkEvent::Connected,
            [this](const NetworkEventData& e) { OnClientConnectionEstablished(e); });

        mClientEngine->RegisterEventCallback(NetworkEvent::Disconnected,
            [this](const NetworkEventData& e) { OnClientConnectionClosed(e); });

        mClientEngine->RegisterEventCallback(NetworkEvent::DataReceived,
            [this](const NetworkEventData& e) { OnClientDataReceived(e); });

        Logger::Info("TestServer initialized on port " + std::to_string(port) +
                     " (engine: " + mEngineType + ")");
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

#ifdef _WIN32
        // Wake reconnect loop immediately (if waiting in backoff sleep)
        mDBShutdownCV.notify_all();

        if (mDBReconnectThread.joinable())
        {
            mDBReconnectThread.join();
        }
#endif

        // Record disconnect events for still-connected sessions before
        //          shutting down DBTaskQueue so Stop() does not lose terminal records.
        if (mDBTaskQueue && mDBTaskQueue->IsRunning())
        {
            std::tm localTime{};
            std::time_t rawNow = std::time(nullptr);
#ifdef _WIN32
            localtime_s(&localTime, &rawNow);
#else
            localtime_r(&rawNow, &localTime);
#endif

            char timeStr[64] = {0};
            std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);
            const std::string shutdownTime(timeStr);

            size_t queuedDisconnectCount = 0;
            // Get session snapshot to avoid race condition with session removal
            auto allSessions = Core::SessionManager::Instance().GetAllSessions();
            for (auto& session : allSessions)
            {
                if (!session || !session->IsConnected())
                {
                    continue;
                }

                mDBTaskQueue->RecordDisconnectTime(session->GetId(), shutdownTime);
                ++queuedDisconnectCount;
            }

            if (queuedDisconnectCount > 0)
            {
                Logger::Info("Queued disconnect records during shutdown: " +
                             std::to_string(queuedDisconnectCount));
            }
        }

        // Shutdown DB ping timer before disconnecting so the last ping isn't lost.
#ifdef _WIN32
        if (mDBPingTimer != 0)
        {
            mTimerQueue.Cancel(mDBPingTimer);
            mDBPingTimer = 0;
        }
        mTimerQueue.Shutdown();
#endif

        // Step 1 - Flush DB task queue (complete pending tasks while DB connection is still alive)
        if (mDBTaskQueue)
        {
            Logger::Info("Shutting down DB task queue...");
            mDBTaskQueue->Shutdown();
            Logger::Info("DB task queue statistics - Processed: " +
                        std::to_string(mDBTaskQueue->GetProcessedCount()) +
                        ", Failed: " + std::to_string(mDBTaskQueue->GetFailedCount()));
        }

        // Disconnect local database after the queue is fully drained.
        if (mLocalDatabase)
        {
            mLocalDatabase->Disconnect();
            mLocalDatabase.reset();
            Logger::Info("TestServer: local database disconnected");
        }

        // Step 2 - Disconnect from DB server BEFORE mClientEngine->Stop()
        //          mClientEngine->Stop() calls WSACleanup() which invalidates mDBServerSocket.
        //          Closing DB socket after WSACleanup causes WSAECONNRESET(10054) in DBRecvLoop.
        DisconnectFromDBServer();

        // Step 3 - Stop client network engine (closes IOCP/RIO, calls WSACleanup)
        if (mClientEngine)
        {
            mClientEngine->Stop();
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

#ifdef _WIN32
        // Store endpoint for reconnect loop
        mDBHost = host;
        mDBPort = port;

        if (mDBRunning.load())
        {
            Logger::Warn("DB server connection already running");
            return true;
        }

        // Join previous recv thread before reusing (reconnect path).
        //          Ping is now handled by TimerQueue — cancel any previous timer.
        if (mDBRecvThread.joinable()) mDBRecvThread.join();
        if (mDBPingTimer != 0)
        {
            mTimerQueue.Cancel(mDBPingTimer);
            mDBPingTimer = 0;
        }

        // Initialize Winsock exactly once (thread-safe via call_once)
        WSADATA wsaData;
        static std::once_flag sWsaInitFlag;
        bool wsaOk = true;
        std::call_once(sWsaInitFlag, [&]() {
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            {
                Logger::Error("WSAStartup failed");
                wsaOk = false;
            }
        });
        if (!wsaOk) return false;

        // Create client socket
        SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (clientSocket == INVALID_SOCKET)
        {
            mLastDBConnectError.store(WSAGetLastError());
            Logger::Error("Failed to create client socket: " + std::to_string(mLastDBConnectError.load()));
            return false;
        }

        // Set up server address
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);

        // Convert host string to address
        if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0)
        {
            Logger::Error("Invalid address: " + host);
            closesocket(clientSocket);
            return false;
        }

        // Connect to DB server
        if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
        {
            mLastDBConnectError.store(WSAGetLastError());
            Logger::Error("Failed to connect to DB server: " + std::to_string(mLastDBConnectError.load()));
            closesocket(clientSocket);
            return false;
        }

        // Reset last error on success
        mLastDBConnectError.store(0);

        // Create and initialize DBServerSession for DB connection
        mDBServerSession = std::make_shared<DBServerSession>();
        mDBServerSession->Initialize(static_cast<uint64_t>(clientSocket), clientSocket);

        mDBServerSocket = clientSocket;
        mDBRunning.store(true);

        // Start DB recv thread.
        //          DB ping is now handled by TimerQueue (fires every PING_INTERVAL_MS).
        //          The repeat callback returns mDBRunning so the timer auto-cancels on disconnect.
        mDBRecvThread = std::thread(&TestServer::DBRecvLoop, this);
        mDBPingTimer  = mTimerQueue.ScheduleRepeat(
            [this]() -> bool
            {
                SendDBPing();
                return mDBRunning.load();
            },
            Core::PING_INTERVAL_MS);

        Logger::Info("Successfully connected to DB server at " + host + ":" + std::to_string(port));
        return true;
#else
        // Non-Windows platforms not yet supported for client connections
        Logger::Error("Client connection not implemented for non-Windows platforms");
        return false;
#endif
    }

    void TestServer::OnClientConnectionEstablished(const NetworkEventData& eventData)
    {
        Logger::Info("Client connected - Connection: " + std::to_string(eventData.connectionId));

        // Record connect time asynchronously.
        //          Replaces ClientSession::OnConnected / AsyncRecordConnectTime.
        if (mDBTaskQueue && mDBTaskQueue->IsRunning())
        {
            auto now = std::chrono::system_clock::now();
            auto rawTime = std::chrono::system_clock::to_time_t(now);
            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &rawTime);
#else
            localtime_r(&rawTime, &localTime);
#endif
            char timeStr[64] = {0};
            std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);
            mDBTaskQueue->RecordConnectTime(eventData.connectionId, timeStr);
        }
    }

    void TestServer::OnClientConnectionClosed(const NetworkEventData& eventData)
    {
        Logger::Info("Client disconnected - Connection: " + std::to_string(eventData.connectionId));

        // Record disconnect time only during normal operation.
        //          Stop() already records disconnect for all active sessions before
        //          engine teardown, so we skip during shutdown to avoid duplicates.
        if (mIsRunning.load() && mDBTaskQueue && mDBTaskQueue->IsRunning())
        {
            auto now = std::chrono::system_clock::now();
            auto rawTime = std::chrono::system_clock::to_time_t(now);
            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &rawTime);
#else
            localtime_r(&rawTime, &localTime);
#endif
            char timeStr[64] = {0};
            std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);
            mDBTaskQueue->RecordDisconnectTime(eventData.connectionId, timeStr);
        }
    }

    void TestServer::OnClientDataReceived(const NetworkEventData& eventData)
    {
        Logger::Debug("Received " + std::to_string(eventData.dataSize) +
            " bytes from client Connection: " + std::to_string(eventData.connectionId));
    }

    void TestServer::DisconnectFromDBServer()
    {
#ifdef _WIN32
        if (!mDBRunning.load())
        {
            return;
        }

        mDBRunning.store(false);

        // Cancel DB ping timer immediately (auto-cancel via return value may lag one interval).
        if (mDBPingTimer != 0)
        {
            mTimerQueue.Cancel(mDBPingTimer);
            mDBPingTimer = 0;
        }

        // Wake reconnect loop immediately if waiting in backoff sleep.
        mDBShutdownCV.notify_all();

        if (mDBServerSocket != INVALID_SOCKET)
        {
            shutdown(mDBServerSocket, SD_BOTH);
        }

        if (mDBRecvThread.joinable())
        {
            mDBRecvThread.join();
        }

        if (mDBServerSession)
        {
            mDBServerSession->Close();
            mDBServerSession.reset();
        }

        mDBServerSocket = INVALID_SOCKET;
        mDBRecvBuffer.clear();
        mDBRecvOffset = 0;
#endif
    }

    bool TestServer::SendDBPacket(const void* data, uint32_t size)
    {
#ifdef _WIN32
        if (!mDBRunning.load() || mDBServerSocket == INVALID_SOCKET || !data || size == 0)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(mDBSendMutex);

        const char* ptr = static_cast<const char*>(data);
        uint32_t remaining = size;
        while (remaining > 0)
        {
            int sent = send(mDBServerSocket, ptr, remaining, 0);
            if (sent == SOCKET_ERROR)
            {
                Logger::Error("Failed to send DB packet: " + std::to_string(WSAGetLastError()));
                return false;
            }
            remaining -= static_cast<uint32_t>(sent);
            ptr += sent;
        }

        return true;
#else
        (void)data;
        (void)size;
        return false;
#endif
    }

    void TestServer::DBRecvLoop()
    {
#ifdef _WIN32
        constexpr size_t kTempBufferSize = 4096;
        std::vector<char> tempBuffer(kTempBufferSize);

        while (mDBRunning.load())
        {
            int received = recv(mDBServerSocket, tempBuffer.data(),
                                static_cast<int>(tempBuffer.size()), 0);
            if (received > 0)
            {
                mDBRecvBuffer.insert(mDBRecvBuffer.end(),
                                     tempBuffer.data(),
                                     tempBuffer.data() + received);

                while (mDBRecvBuffer.size() - mDBRecvOffset >= sizeof(Core::ServerPacketHeader))
                {
                    const auto* header = reinterpret_cast<const Core::ServerPacketHeader*>(
                        mDBRecvBuffer.data() + mDBRecvOffset);

                    if (header->size < sizeof(Core::ServerPacketHeader) || header->size > 4096)
                    {
                        Logger::Warn("Invalid DB packet size: " + std::to_string(header->size));
                        mDBRecvBuffer.clear();
                        mDBRecvOffset = 0;
                        break;
                    }

                    if (mDBRecvBuffer.size() - mDBRecvOffset < header->size)
                    {
                        break;
                    }

                    auto sessionSnapshot = mDBServerSession;
                    if (sessionSnapshot)
                    {
                        sessionSnapshot->OnRecv(
                            mDBRecvBuffer.data() + mDBRecvOffset,
                            header->size);
                    }

                    mDBRecvOffset += header->size;
                }

                // Compact buffer when offset exceeds half the buffer size
                if (mDBRecvOffset > 0 && mDBRecvOffset > mDBRecvBuffer.size() / 2)
                {
                    mDBRecvBuffer.erase(mDBRecvBuffer.begin(),
                                        mDBRecvBuffer.begin() + mDBRecvOffset);
                    mDBRecvOffset = 0;
                }
            }
            else if (received == 0)
            {
                Logger::Warn("DB server closed connection");
                break;
            }
            else
            {
                int error = WSAGetLastError();
                if (error == WSAEINTR)
                {
                    continue;
                }
                Logger::Error("DB recv failed: " + std::to_string(error));
                break;
            }
        }

        mDBRunning.store(false);

        // If server is still running and no reconnect is active, start one
        if (mIsRunning.load() && !mDBReconnectRunning.load())
        {
            mDBReconnectRunning.store(true);
            if (mDBReconnectThread.joinable()) mDBReconnectThread.join();
            mDBReconnectThread = std::thread(&TestServer::DBReconnectLoop, this);
        }
#endif
    }

    // DBPingLoop() removed — DB ping is now handled by mTimerQueue.ScheduleRepeat()
    //          in ConnectToDBServer(). See SendDBPing() for the ping logic.

    void TestServer::DBReconnectLoop()
    {
#ifdef _WIN32
        // Exponential backoff: 1s, 2s, 4s, 8s, 16s, max 30s
        //          Exception: WSAECONNREFUSED (server shutting down / starting up)
        //          → short fixed 1s interval to catch fast restarts
        constexpr uint32_t kMaxDelayMs = 30000;
        constexpr uint32_t kConnRefusedDelayMs = 1000;
        uint32_t delayMs = 1000;
        int attempt = 0;

        while (mIsRunning.load() && !mDBRunning.load())
        {
            ++attempt;
            Logger::Info("DB reconnect attempt #" + std::to_string(attempt) +
                         " in " + std::to_string(delayMs) + "ms...");

            // Wait with CV so Stop() can interrupt immediately
            {
                std::unique_lock<std::mutex> lock(mDBShutdownMutex);
                mDBShutdownCV.wait_for(lock,
                    std::chrono::milliseconds(delayMs),
                    [this] { return !mIsRunning.load(); });
            }

            if (!mIsRunning.load()) break;

            if (ConnectToDBServer(mDBHost, mDBPort))
            {
                Logger::Info("DB reconnected successfully after " +
                             std::to_string(attempt) + " attempt(s)");
                break;
            }

            // Distinguish WSAECONNREFUSED from other errors:
            //   WSAECONNREFUSED(10061): DB server is shutting down or starting up
            //   → Use short fixed interval (no backoff growth) to catch fast restarts
            //   Other errors: Apply standard exponential backoff
            int lastError = mLastDBConnectError.load();
            if (lastError == WSAECONNREFUSED)
            {
                delayMs = kConnRefusedDelayMs;
                Logger::Info("DB server is shutting down or starting up (CONNREFUSED), retrying in 1s...");
            }
            else
            {
                delayMs = std::min(delayMs * 2, kMaxDelayMs);
            }
        }

        mDBReconnectRunning.store(false);
#endif
    }

} // namespace Network::TestServer
