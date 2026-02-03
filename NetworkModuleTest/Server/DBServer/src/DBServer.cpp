// English: Database Server implementation
// ???: ?怨쀬뵠?怨뺤퓢??곷뮞 ??뺤쒔 ?닌뗭겱



#include <chrono>
#include <cstring>
#include <iostream>
#include "../include/DBServer.h"
// 한글: AsyncIOProvider 정의는 ServerEngine 경로의 헤더로 통일한다.

using namespace Network::AsyncIO;
using namespace Network::Protocols;

namespace Network
{
namespace DBServer
{
// =============================================================================
// English: Constructor and Destructor
// ???: ??밴쉐??獄????늾??
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
// English: Lifecycle management
// ???: ??몄구雅뚯눊由??온??
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
    // 한글: Ping/Pong 시간 기록용 DB 처리 모듈 준비
    mDbProcessingModule = std::make_unique<DBProcessingModule>();

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
// English: Network event handlers
// ???: ??쎈뱜??곌쾿 ??源???紐껊굶??
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

    // 한글: Ping/Pong 시간을 GMT 기준으로 기록한다.
    if (mDbProcessingModule)
    {
        mDbProcessingModule->RecordPingPongTimeUtc(
            message.mConnectionId, mPingPongHandler->GetLastPingTimestamp(),
            mPingPongHandler->GetLastPongTimestamp());
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

    // 한글: TestServer로부터 받은 Pong 응답의 RTT를 로그로 남긴다.
    std::cout << "Pong message processed - RTT: " << rtt << " ms" << std::endl;
}

// =============================================================================
// English: Database operations
// ???: ?怨쀬뵠?怨뺤퓢??곷뮞 ?臾믩씜
// =============================================================================

bool DBServer::ConnectToDatabase()
{
    // For now, just print connection info
    // In real implementation, connect to actual database
    std::cout << "Connecting to database:" << std::endl;
    std::cout << "  Host: " << mDbConfig.host << std::endl;
    std::cout << "  Port: " << mDbConfig.port << std::endl;
    std::cout << "  Database: " << mDbConfig.database << std::endl;
    std::cout << "  Username: " << mDbConfig.username << std::endl;

    // Simulate successful connection for now
    return true;
}

void DBServer::DisconnectFromDatabase()
{
    std::cout << "Disconnecting from database" << std::endl;
    // In real implementation, close database connection
}

std::string DBServer::ExecuteQuery(const std::string &query)
{
    std::cout << "Executing query: " << query << std::endl;
    // In real implementation, execute actual query and return results
    return "{\"status\": \"success\", \"message\": \"Query executed\"}";
}

// =============================================================================
// English: Private methods
// ???: ??쑨?у첎?筌롫뗄???
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
