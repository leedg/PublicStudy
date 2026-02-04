// English: GameSession implementation
// 한글: GameSession 구현

#include "../include/GameSession.h"
#include "Utils/NetworkUtils.h"
#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>

#ifdef ENABLE_DATABASE_SUPPORT
#include "Database/DBConnectionPool.h"
using namespace Network::Database;
#endif

namespace Network::TestServer
{
    using namespace Network::Core;
    using namespace Network::Utils;

    GameSession::GameSession()
        : mConnectionRecorded(false)
        , mPacketHandler(std::make_unique<ClientPacketHandler>())
    {
    }

    GameSession::~GameSession()
    {
    }

    void GameSession::OnConnected()
    {
        Logger::Info("GameSession connected - ID: " + std::to_string(GetId()));

        // English: Record connect time to DB on first connection (async)
        // 한글: 최초 접속 시 DB에 접속 시간 기록 (비동기)
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
        if (mPacketHandler)
        {
            mPacketHandler->ProcessPacket(this, data, size);
        }
    }

    void GameSession::RecordConnectTimeToDB()
    {
#ifdef ENABLE_DATABASE_SUPPORT
        ConnectionId sessionId = GetId();

        // English: Get current time string
        // 한글: 현재 시간 문자열 조회
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
        // 한글: 접속 풀을 통해 DB 쿼리 실행
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

} // namespace Network::TestServer
