// English: Database Server implementation
// 한글: 데이터베이스 서버 구현

#include <chrono>
#include <cstring>
#include <iostream>
#include "../include/DBServer.h"
// ConnectToDatabase를 위한 IDatabase / DatabaseFactory 전체 정의 필요
#include "../../ServerEngine/Interfaces/IDatabase.h"
#include "../../ServerEngine/Database/DatabaseFactory.h"
#include "../../ServerEngine/Database/SqlScriptRunner.h"
// English: SessionManager and Session needed to retrieve recv buffer in WorkerThread
// 한글: WorkerThread에서 recv 버퍼 조회를 위한 SessionManager / Session 포함
#include "../../ServerEngine/Network/Core/SessionManager.h"
#include "../../ServerEngine/Network/Core/Session.h"
// 한글: AsyncIOProvider 정의는 ServerEngine 경로의 헤더로 통일한다.

using namespace Network::AsyncIO;
using namespace Network::Protocols;

namespace Network
{
namespace DBServer
{
namespace
{
bool LooksLikeConnectionString(const std::string& value)
{
    return value.find('=') != std::string::npos &&
           value.find(';') != std::string::npos;
}
} // namespace

std::string DBServer::BuildConnectionString(const DatabaseConfig& config)
{
    using Network::Database::DatabaseException;
    using Network::Database::DatabaseType;

    if (!config.connectionString.empty())
    {
        return config.connectionString;
    }

    if (LooksLikeConnectionString(config.host))
    {
        return config.host;
    }

    if (config.type == DatabaseType::SQLite)
    {
        return config.database;
    }

    if (config.type == DatabaseType::ODBC || config.type == DatabaseType::OLEDB ||
        config.type == DatabaseType::MySQL ||
        config.type == DatabaseType::PostgreSQL)
    {
        Network::Database::DatabaseConfig dbConfig;
        dbConfig.mType = (config.type == DatabaseType::OLEDB)
                             ? DatabaseType::OLEDB
                             : DatabaseType::ODBC;
        dbConfig.mSqlDialectHint = config.sqlDialectHint;
        if (dbConfig.mSqlDialectHint == Network::Database::SqlDialect::Auto)
        {
            if (config.type == DatabaseType::MySQL)
            {
                dbConfig.mSqlDialectHint = Network::Database::SqlDialect::MySQL;
            }
            else if (config.type == DatabaseType::PostgreSQL)
            {
                dbConfig.mSqlDialectHint =
                    Network::Database::SqlDialect::PostgreSQL;
            }
            else if (config.type == DatabaseType::OLEDB)
            {
                dbConfig.mSqlDialectHint = Network::Database::SqlDialect::SQLServer;
            }
        }
        dbConfig.mHost = config.host;
        dbConfig.mPort = config.port;
        dbConfig.mDatabase = config.database;
        dbConfig.mUser = config.username;
        dbConfig.mPassword = config.password;
        return (dbConfig.mType == DatabaseType::OLEDB)
                   ? dbConfig.BuildOLEDBConnectionString()
                   : dbConfig.BuildODBCConnectionString();
    }

    throw DatabaseException(
        "DBServer requires SetDatabaseConnectionString() for this backend type");
}
// =============================================================================
// 생성자 / 소멸자
// =============================================================================

DBServer::DBServer()
    : mIsRunning(false), mIsInitialized(false), mPort(8002),
      mMaxConnections(1000)
{
}

DBServer::~DBServer()
{
    if (mIsRunning.load(std::memory_order_acquire))
    {
        Stop();
    }
}

// =============================================================================
// 생명주기 관리
// =============================================================================

bool DBServer::Initialize(uint16_t port, size_t maxConnections)
{
    if (mIsInitialized.load(std::memory_order_acquire))
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
    // 통합 레이턴시 관리자 (RTT 통계 + 핑 시간 저장)
    // 이전에 사용하던 별도 DBPingTimeManager를 대체.
    mLatencyManager = std::make_unique<ServerLatencyManager>();
    mLatencyManager->Initialize();

    // Register message handlers
    mMessageHandler->RegisterHandler(
        MessageType::Ping, [this](const Message &msg) { OnPingMessage(msg); });
    mMessageHandler->RegisterHandler(
        MessageType::Pong, [this](const Message &msg) { OnPongMessage(msg); });

    mIsInitialized.store(true, std::memory_order_release);
    std::cout << "DBServer initialized on port " << port << std::endl;
    return true;
}

bool DBServer::Start()
{
    if (!mIsInitialized.load(std::memory_order_acquire))
    {
        std::cerr << "DBServer not initialized" << std::endl;
        return false;
    }

    if (mIsRunning.load(std::memory_order_acquire))
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

    mIsRunning.store(true, std::memory_order_release);

    // Start worker thread
    mWorkerThread = std::thread(&DBServer::WorkerThread, this);

    std::cout << "DBServer started successfully" << std::endl;
    return true;
}

void DBServer::Stop()
{
    if (!mIsRunning.load(std::memory_order_acquire))
        return;

    mIsRunning.store(false, std::memory_order_release);

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

bool DBServer::IsRunning() const { return mIsRunning.load(std::memory_order_acquire); }

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
    mDbConfig.connectionString.clear();
}

void DBServer::SetDatabaseType(Network::Database::DatabaseType type)
{
    mDbConfig.type = type;
}

void DBServer::SetDatabaseSqlDialectHint(
    Network::Database::SqlDialect sqlDialectHint)
{
    mDbConfig.sqlDialectHint = sqlDialectHint;
}

void DBServer::SetDatabaseConnectionString(
    Network::Database::DatabaseType type,
    const std::string &connectionString,
    Network::Database::SqlDialect sqlDialectHint)
{
    mDbConfig.type = type;
    mDbConfig.connectionString = connectionString;
    mDbConfig.sqlDialectHint = sqlDialectHint;
}

// =============================================================================
// 네트워크 이벤트 핸들러
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

    // 통합 레이턴시 관리자로 핑 시간 기록 (GMT 기준)
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

    // TestServer로부터 받은 Pong 응답의 RTT를 로그로 남긴다.
    std::cout << "Pong message processed - RTT: " << rtt << " ms" << std::endl;
}

// =============================================================================
// 데이터베이스 작업
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
            // Mock 외 백엔드를 위한 연결 문자열 구성
            Network::Database::DatabaseConfig dbConfig;
            dbConfig.mType = mDbConfig.type;
            dbConfig.mConnectionString = BuildConnectionString(mDbConfig);
            if (mDbConfig.sqlDialectHint != Network::Database::SqlDialect::Auto)
            {
                dbConfig.mSqlDialectHint = mDbConfig.sqlDialectHint;
            }
            else if (mDbConfig.type == DatabaseType::OLEDB)
            {
                dbConfig.mSqlDialectHint = Network::Database::SqlDialect::SQLServer;
            }
            else
            {
                dbConfig.mSqlDialectHint =
                    Network::Database::SqlScriptRunner::InferSqlDialectHint(
                        dbConfig.mConnectionString);
            }
            mDatabase->Connect(dbConfig);
        }
        else
        {
            // MockDatabase — 기본 config로 Connect() 호출
            Network::Database::DatabaseConfig dbConfig;
            dbConfig.mType = DatabaseType::Mock;
            mDatabase->Connect(dbConfig);
        }

        std::cout << "Database connected successfully" << std::endl;

        // 레이턴시 관리자에 DB 주입하여 테이블 저장 활성화
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
        // 연결 해제 전 레이턴시 관리자에서 DB 참조 제거
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
// 내부 메서드
// =============================================================================

void DBServer::WorkerThread()
{
    std::cout << "DBServer worker thread started" << std::endl;

    while (mIsRunning.load(std::memory_order_acquire))
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
                {
                    // English: Retrieve the session's recv buffer and deliver to the
                    //          message handler pipeline. CompletionEntry carries only the
                    //          byte count (mResult); the actual bytes live in the session.
                    // 한글: 세션의 recv 버퍼를 조회하여 메시지 핸들러 파이프라인으로 전달.
                    //       CompletionEntry는 바이트 수(mResult)만 보유; 실제 데이터는 세션에 있음.
                    if (entry.mResult > 0)
                    {
                        auto session = Core::SessionManager::Instance().GetSession(
                            static_cast<Utils::ConnectionId>(entry.mContext));
                        if (session)
                        {
                            const char *buf = session->GetRecvBuffer();
                            OnDataReceived(entry.mContext,
                                           reinterpret_cast<const uint8_t *>(buf),
                                           static_cast<size_t>(entry.mResult));
                            if (!session->PostRecv())
                            {
                                OnConnectionClosed(entry.mContext);
                            }
                        }
                        else
                        {
                            // English: Session not found in SessionManager — base DBServer does not
                            //          register sessions via SessionManager. Use TestDBServer (engine-
                            //          based) for full recv dispatch. This log surfaces the issue.
                            // 한글: SessionManager에 세션 없음 — base DBServer는 SessionManager로
                            //       세션을 등록하지 않음. 완전한 recv 처리는 TestDBServer 사용.
                            std::cerr << "[WARN] DBServer::WorkerThread: recv "
                                      << entry.mResult << " bytes on conn "
                                      << entry.mContext
                                      << " dropped — session not in SessionManager"
                                      << std::endl;
                        }
                    }
                    break;
                }

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
