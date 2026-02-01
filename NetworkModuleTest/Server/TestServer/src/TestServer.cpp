// English: TestServer implementation
// ?쒓?: TestServer 援ы쁽

#include "../include/TestServer.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>

using namespace Network::Core;
using namespace Network::Utils;

#ifdef ENABLE_DATABASE_SUPPORT
using namespace Network::Database;
#endif

namespace Network::TestServer
{

// =============================================================================
// English: GameSession implementation
// ?쒓?: GameSession 援ы쁽
// =============================================================================

GameSession::GameSession()
    : mConnectionRecorded(false)
{
}

GameSession::~GameSession()
{
}

void GameSession::OnConnected()
{
    Logger::Info("GameSession connected - ID: " + std::to_string(GetId()));

    // English: Record connect time to DB on first connection (async)
    // ?쒓?: 理쒖큹 ?묒냽 ??DB???곌껐 ?쒓컙 湲곕줉 (鍮꾨룞湲?
    if (!mConnectionRecorded)
    {
        RecordConnectTimeToDB();
        mConnectionRecorded = true;
    }
}

void GameSession::OnDisconnected()
{
    Logger::Info("GameSession disconnected - ID: " + std::to_string(GetId()));
}

void GameSession::OnRecv(const char* data, uint32_t size)
{
    if (size < sizeof(PacketHeader))
    {
        Logger::Warn("Packet too small - size: " + std::to_string(size));
        return;
    }

    const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data);

    if (header->size > size)
    {
        Logger::Warn("Incomplete packet - expected: " + std::to_string(header->size) +
                     ", received: " + std::to_string(size));
        return;
    }

    ProcessPacket(header, data);
}

void GameSession::ProcessPacket(const PacketHeader* header, const char* data)
{
    PacketType packetType = static_cast<PacketType>(header->id);

    switch (packetType)
    {
    case PacketType::SessionConnectReq:
        HandleConnectRequest(reinterpret_cast<const PKT_SessionConnectReq*>(data));
        break;

    case PacketType::PingReq:
        HandlePingRequest(reinterpret_cast<const PKT_PingReq*>(data));
        break;

    default:
        Logger::Warn("Unknown packet type: " + std::to_string(header->id));
        break;
    }
}

void GameSession::HandleConnectRequest(const PKT_SessionConnectReq* packet)
{
    Logger::Info("Connect request - Session: " + std::to_string(GetId()) +
                 ", ClientVersion: " + std::to_string(packet->clientVersion));

    // English: Send connect response
    // ?쒓?: ?곌껐 ?묐떟 ?꾩넚
    PKT_SessionConnectRes response;
    response.sessionId = GetId();
    response.serverTime = static_cast<uint32_t>(std::time(nullptr));
    response.result = static_cast<uint8_t>(ConnectResult::Success);

    Send(response);
}

void GameSession::HandlePingRequest(const PKT_PingReq* packet)
{
    SetLastPingTime(Timer::GetCurrentTimestamp());

    // English: Send pong response
    // ?쒓?: ???묐떟 ?꾩넚
    PKT_PongRes response;
    response.clientTime = packet->clientTime;
    response.serverTime = Timer::GetCurrentTimestamp();
    response.sequence = packet->sequence;

    Send(response);

    Logger::Debug("Ping/Pong - Session: " + std::to_string(GetId()) +
                  ", Seq: " + std::to_string(packet->sequence));
}

void GameSession::RecordConnectTimeToDB()
{
#ifdef ENABLE_DATABASE_SUPPORT
    ConnectionId sessionId = GetId();

    // English: Get current time string
    // ?쒓?: ?꾩옱 ?쒓컙 臾몄옄??議고쉶
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime;
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    char timeStr[64];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &localTime);

    // English: Execute DB query via connection pool
    // ?쒓?: ?곌껐 ????듯빐 DB 荑쇰━ ?ㅽ뻾
    if (!DBConnectionPool::Instance().IsInitialized())
    {
        Logger::Info("DB not initialized - skipping connect time recording for Session: " +
                     std::to_string(sessionId));
        return;
    }

    ScopedDBConnection dbConn;

    if (dbConn.IsValid())
    {
        std::ostringstream query;
        query << "INSERT INTO SessionLog (SessionId, ConnectTime) VALUES ("
              << sessionId << ", '" << timeStr << "')";

        if (dbConn->Execute(query.str()))
        {
            Logger::Info("Connect time recorded - Session: " + std::to_string(sessionId));
        }
        else
        {
            Logger::Error("Failed to record connect time - Session: " +
                          std::to_string(sessionId) + " - " + dbConn->GetLastError());
        }
    }
    else
    {
        Logger::Warn("No DB connection available for Session: " + std::to_string(sessionId));
    }
#else
    Logger::Info("Database support disabled - skipping connect time recording");
#endif
}

// =============================================================================
// English: TestServer implementation
// ?쒓?: TestServer 援ы쁽
// =============================================================================

TestServer::TestServer()
    : mIsRunning(false), mPort(0)
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
    // English: Initialize SessionManager with GameSession factory
    // ?쒓?: GameSession ?⑺넗由щ줈 SessionManager 珥덇린??
    // SessionManager::Instance().SetFactory(&TestServer::CreateGameSession);

    // English: Initialize DB connection pool (optional)
    // ?쒓?: DB ?곌껐 ? 珥덇린??(?좏깮)
#ifdef ENABLE_DATABASE_SUPPORT
    if (!dbConnectionString.empty())
    {
        if (!DBConnectionPool::Instance().Initialize(dbConnectionString, 5))
        {
            Logger::Warn("Failed to initialize DB pool (continuing without DB)");
        }
    }
    else
    {
        Logger::Info("No DB connection string - running without DB");
    }
#else
    (void)dbConnectionString; // Suppress unused warning
    Logger::Info("Database support disabled at compile time");
#endif

    // English: Create and initialize network engine
    // 한글: 네트워크 엔진 생성 및 초기화
    mEngine = std::unique_ptr<Core::IOCPNetworkEngine>(new Core::IOCPNetworkEngine());

    if (!mEngine->Initialize(MAX_CONNECTIONS, port))
    {
        Logger::Error("Failed to initialize network engine");
        return false;
    }

    // English: Register event callbacks
    // ?쒓?: ?대깽??肄쒕갚 ?깅줉
    mEngine->RegisterEventCallback(NetworkEvent::Connected,
        [this](const NetworkEventData& e) { OnConnectionEstablished(e); });

    mEngine->RegisterEventCallback(NetworkEvent::Disconnected,
        [this](const NetworkEventData& e) { OnConnectionClosed(e); });

    mEngine->RegisterEventCallback(NetworkEvent::DataReceived,
        [this](const NetworkEventData& e) { OnDataReceived(e); });

    Logger::Info("TestServer initialized on port " + std::to_string(port));
    return true;
}

bool TestServer::Start()
{
    if (!mEngine)
    {
        Logger::Error("TestServer not initialized");
        return false;
    }

    if (!mEngine->Start())
    {
        Logger::Error("Failed to start network engine");
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

    if (mEngine)
    {
        mEngine->Stop();
    }

    // English: Shutdown DB pool
    // ?쒓?: DB ? 醫낅즺
#ifdef ENABLE_DATABASE_SUPPORT
    if (DBConnectionPool::Instance().IsInitialized())
    {
        DBConnectionPool::Instance().Shutdown();
    }
#endif

    Logger::Info("TestServer stopped");
}

bool TestServer::IsRunning() const
{
    return mIsRunning.load();
}

void TestServer::OnConnectionEstablished(const NetworkEventData& eventData)
{
    Logger::Info("Client accepted - Connection: " + std::to_string(eventData.connectionId));
}

void TestServer::OnConnectionClosed(const NetworkEventData& eventData)
{
    Logger::Info("Client disconnected - Connection: " + std::to_string(eventData.connectionId));
}

void TestServer::OnDataReceived(const NetworkEventData& eventData)
{
    Logger::Debug("Received " + std::to_string(eventData.dataSize) +
                  " bytes from Connection: " + std::to_string(eventData.connectionId));
}

SessionRef TestServer::CreateGameSession()
{
    return std::make_shared<GameSession>();
}

} // namespace Network::TestServer

