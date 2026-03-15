// Database Server implementation



#include <chrono>
#include <cstring>
#include <iostream>
#include "../include/DBServer.h"
// Full IDatabase + DatabaseFactory definitions needed for ConnectToDatabase
#include "../../ServerEngine/Interfaces/IDatabase.h"
#include "../../ServerEngine/Database/DatabaseFactory.h"

using namespace Network::AsyncIO;
using namespace Network::Protocols;

namespace Network
{
namespace DBServer
{
// =============================================================================
// Constructor and Destructor
// =============================================================================

DBServer::DBServer()
    : mIsRunning(false), mIsInitialized(false), mPort(8002),
      mMaxConnections(1000)
{
}

DBServer::~DBServer()
{
    if (mIsRunning)
    {
        Stop();
    }
}

// =============================================================================
// Lifecycle management
// =============================================================================

bool DBServer::Initialize(uint16_t port, size_t maxConnections)
{
    if (mIsInitialized)
    {
        std::cerr << "DBServer already initialized" << std::endl;
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
    auto error = mAsyncIOProvider->Initialize(256, maxConnections);
    if (error != AsyncIOError::Success)
    {
        std::cerr << "Failed to initialize AsyncIO provider: "
                  << static_cast<int>(error) << std::endl;
        return false;
    }

    // Create message handler
    mMessageHandler = std::make_unique<MessageHandler>();
    mPingPongHandler = std::make_unique<PingPongHandler>();
    // Unified latency manager (RTT stats + ping time persistence)
    //          Replaces the separate DBPingTimeManager that was used here.
    mLatencyManager = std::make_unique<ServerLatencyManager>();
    mLatencyManager->Initialize();

    // Register message handlers
    mMessageHandler->RegisterHandler(
        MessageType::Ping, [this](const Message &msg) { OnPingMessage(msg); });
    mMessageHandler->RegisterHandler(
        MessageType::Pong, [this](const Message &msg) { OnPongMessage(msg); });

    mIsInitialized = true;
    std::cout << "DBServer initialized on port " << port << std::endl;
    return true;
}

bool DBServer::Start()
{
    if (!mIsInitialized)
    {
        std::cerr << "DBServer not initialized" << std::endl;
        return false;
    }

    if (mIsRunning)
    {
        std::cerr << "DBServer already running" << std::endl;
        return false;
    }

    // Connect to database
    if (!ConnectToDatabase())
    {
        std::cerr << "Failed to connect to database" << std::endl;
        return false;
    }

    mIsRunning = true;

    // Start worker thread
    mWorkerThread = std::thread(&DBServer::WorkerThread, this);

    std::cout << "DBServer started successfully" << std::endl;
    return true;
}

void DBServer::Stop()
{
    if (!mIsRunning)
        return;

    mIsRunning = false;

    // Wait for worker thread to finish
    if (mWorkerThread.joinable())
    {
        mWorkerThread.join();
    }

    // Disconnect from database
    DisconnectFromDatabase();

    // Shutdown AsyncIO provider
    if (mAsyncIOProvider)
    {
        mAsyncIOProvider->Shutdown();
    }

    std::cout << "DBServer stopped" << std::endl;
}

bool DBServer::IsRunning() const { return mIsRunning; }

void DBServer::SetDatabaseConfig(const std::string &host, uint16_t port,
                                 const std::string &database,
                                 const std::string &username,
                                 const std::string &password)
{
    mDbConfig.host = host;
    mDbConfig.port = port;
    mDbConfig.database = database;
    mDbConfig.username = username;
    mDbConfig.password = password;
}

// =============================================================================
// Network event handlers
// =============================================================================

void DBServer::OnConnectionEstablished(ConnectionId connectionId)
{
    std::lock_guard<std::mutex> lock(mConnectionsMutex);
    mConnections[connectionId] = "unknown";

    std::cout << "New connection established: " << connectionId << std::endl;
}

void DBServer::OnConnectionClosed(ConnectionId connectionId)
{
    std::lock_guard<std::mutex> lock(mConnectionsMutex);
    mConnections.erase(connectionId);

    std::cout << "Connection closed: " << connectionId << std::endl;
}

void DBServer::OnDataReceived(ConnectionId connectionId, const uint8_t *data,
                              size_t size)
{
    if (!data || size == 0)
        return;

    // Process message through message handler
    mMessageHandler->ProcessMessage(connectionId, data, size);
}

void DBServer::OnPingMessage(const Message &message)
{
    // Create pong response
    auto pongData =
        mPingPongHandler->CreatePong(message.mData, "DBServer Pong Response");

    if (pongData.empty())
    {
        std::cerr << "Invalid ping message received" << std::endl;
        return;
    }

    // Record ping time via unified latency manager
    if (mLatencyManager && mLatencyManager->IsInitialized())
    {
        mLatencyManager->SavePingTime(
            static_cast<uint32_t>(message.mConnectionId),
            "TestServer",
            mPingPongHandler->GetLastPingTimestamp());
    }

    SendMessage(message.mConnectionId, MessageType::Pong, pongData.data(),
                pongData.size());

    std::cout << "Ping message processed, Pong sent to: "
              << message.mConnectionId << std::endl;
}

void DBServer::OnPongMessage(const Message &message)
{
    if (!mPingPongHandler->ParsePong(message.mData))
    {
        std::cerr << "Invalid pong message received" << std::endl;
        return;
    }

    const auto rtt = mPingPongHandler->CalculateRTT(
        mPingPongHandler->GetLastPongPingTimestamp(),
        mPingPongHandler->GetLastPongTimestamp());

    std::cout << "Pong message processed - RTT: " << rtt << " ms" << std::endl;
}

// =============================================================================
// Database operations
// =============================================================================

bool DBServer::ConnectToDatabase()
{
    using namespace Network::Database;

    std::cout << "Connecting to database (type=" <<
        static_cast<int>(mDbConfig.type) << ")..." << std::endl;

    try
    {
        mDatabase = DatabaseFactory::CreateDatabase(mDbConfig.type);

        if (mDbConfig.type != DatabaseType::Mock)
        {
            // Build a connection string for non-mock backends
            Network::Database::DatabaseConfig dbConfig;
            dbConfig.mType = mDbConfig.type;
            dbConfig.mConnectionString = mDbConfig.database; // SQLite uses file path
            mDatabase->Connect(dbConfig);
        }
        else
        {
            // MockDatabase — just call Connect() with a default config
            Network::Database::DatabaseConfig dbConfig;
            dbConfig.mType = DatabaseType::Mock;
            mDatabase->Connect(dbConfig);
        }

        std::cout << "Database connected successfully" << std::endl;

        // Inject DB into latency manager so it can persist to tables
        if (mLatencyManager)
        {
            mLatencyManager->SetDatabase(mDatabase.get());
        }

        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to connect to database: " << e.what() << std::endl;
        mDatabase.reset();
        return false;
    }
}

void DBServer::DisconnectFromDatabase()
{
    if (mDatabase)
    {
        // Remove DB reference from latency manager before disconnecting
        if (mLatencyManager)
        {
            mLatencyManager->SetDatabase(nullptr);
        }

        mDatabase->Disconnect();
        mDatabase.reset();
        std::cout << "Database disconnected" << std::endl;
    }
}

std::string DBServer::ExecuteQuery(const std::string &query)
{
    std::cout << "Executing query: " << query << std::endl;
    // In real implementation, execute actual query and return results
    return "{\"status\": \"success\", \"message\": \"Query executed\"}";
}

// =============================================================================
// Private methods
// =============================================================================

void DBServer::WorkerThread()
{
    std::cout << "DBServer worker thread started" << std::endl;

    while (mIsRunning)
    {
        // Process completion events
        const int MAX_EVENTS = 64;
        AsyncIO::CompletionEntry entries[MAX_EVENTS];

        int numEvents = mAsyncIOProvider->ProcessCompletions(
            entries, MAX_EVENTS, 100 // 100ms timeout
        );

        if (numEvents > 0)
        {
            for (int i = 0; i < numEvents; ++i)
            {
                const auto &entry = entries[i];

                // Handle different completion types
                switch (entry.mType)
                {
                case AsyncIO::AsyncIOType::Accept:
                    OnConnectionEstablished(entry.mContext);
                    break;

                case AsyncIO::AsyncIOType::Recv:
                    // Handle received data
                    // For now, just log
                    std::cout << "Received " << entry.mResult
                              << " bytes on connection " << entry.mContext
                              << std::endl;
                    break;

                case AsyncIO::AsyncIOType::Send:
                    // Send completed
                    std::cout << "Send completed for connection "
                              << entry.mContext << std::endl;
                    break;

                default:
                    break;
                }
            }
        }

        // Small sleep to prevent busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "DBServer worker thread stopped" << std::endl;
}

void DBServer::SendMessage(ConnectionId connectionId, MessageType type,
                           const void *data, size_t size)
{
    auto message =
        mMessageHandler->CreateMessage(type, connectionId, data, size);

    // For now, just log the message
    std::cout << "Sending message type " << static_cast<uint32_t>(type)
              << " to connection " << connectionId << std::endl;

    // In real implementation, send through AsyncIO provider
    // mAsyncIOProvider->SendAsync(socket, message.data(), message.size(),
    // context);
}

uint64_t DBServer::GetCurrentTimestamp() const
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
        .count();
}

} // namespace DBServer
} // namespace Network
