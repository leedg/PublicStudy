// DB Ping 시간 관리자 구현 (DEPRECATED — 기능이 ServerLatencyManager에 통합됨)

#include "../include/DBPingTimeManager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

namespace Network::DBServer
{
    DBPingTimeManager::DBPingTimeManager()
        : mInitialized{false}
    {
    }

    DBPingTimeManager::~DBPingTimeManager()
    {
        if (mInitialized)
        {
            Shutdown();
        }
    }

    bool DBPingTimeManager::Initialize()
    {
        if (mInitialized.load(std::memory_order_acquire))
        {
            std::cerr << "DBPingTimeManager already initialized" << std::endl;
            return false;
        }

        std::cout << "Initializing DBPingTimeManager..." << std::endl;

        std::string createTableQuery = R"(
            CREATE TABLE IF NOT EXISTS T_PingTimeLog (
                Id INTEGER PRIMARY KEY AUTOINCREMENT,
                ServerId INTEGER NOT NULL,
                ServerName VARCHAR(32),
                PingTimestamp BIGINT NOT NULL,
                PingTimeGMT VARCHAR(32) NOT NULL,
                CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        )";

        // ExecuteQuery(createTableQuery);

        mInitialized.store(true, std::memory_order_release);
        std::cout << "DBPingTimeManager initialized successfully" << std::endl;
        return true;
    }

    void DBPingTimeManager::Shutdown()
    {
        if (!mInitialized.load(std::memory_order_acquire))
            return;

        std::cout << "Shutting down DBPingTimeManager..." << std::endl;

        mInitialized.store(false, std::memory_order_release);
        std::cout << "DBPingTimeManager shut down" << std::endl;
    }

    bool DBPingTimeManager::SavePingTime(uint32_t serverId, const std::string& serverName, uint64_t timestamp)
    {
        if (!mInitialized.load(std::memory_order_acquire))
        {
            std::cerr << "DBPingTimeManager not initialized" << std::endl;
            return false;
        }

        std::lock_guard<std::mutex> lock(mMutex);

        std::string gmtTimeStr = FormatTimestamp(timestamp);

        std::ostringstream query;
        query << "INSERT INTO T_PingTimeLog (ServerId, ServerName, PingTimestamp, PingTimeGMT) VALUES ("
              << serverId << ", '"
              << serverName << "', "
              << timestamp << ", '"
              << gmtTimeStr << "')";

        std::cout << "Saving ping time - ServerId: " << serverId
                  << ", ServerName: " << serverName
                  << ", Timestamp: " << timestamp
                  << ", GMT: " << gmtTimeStr << std::endl;

        bool result = ExecuteQuery(query.str());

        if (result)
        {
            std::cout << "Ping time saved successfully" << std::endl;
        }
        else
        {
            std::cerr << "Failed to save ping time" << std::endl;
        }

        return result;
    }

    uint64_t DBPingTimeManager::GetLastPingTime(uint32_t serverId)
    {
        if (!mInitialized.load(std::memory_order_acquire))
        {
            std::cerr << "DBPingTimeManager not initialized" << std::endl;
            return 0;
        }

        std::lock_guard<std::mutex> lock(mMutex);

        std::ostringstream query;
        query << "SELECT PingTimestamp FROM T_PingTimeLog WHERE ServerId = "
              << serverId
              << " ORDER BY Id DESC LIMIT 1";

        std::cout << "Querying last ping time for ServerId: " << serverId << std::endl;

        return 0;
    }

    std::string DBPingTimeManager::FormatTimestamp(uint64_t timestamp)
    {
        time_t seconds = static_cast<time_t>(timestamp / 1000);

        std::tm gmtTime{};
#ifdef _WIN32
        gmtime_s(&gmtTime, &seconds);
#else
        gmtime_r(&seconds, &gmtTime);
#endif

        std::ostringstream oss;
        oss << std::put_time(&gmtTime, "%Y-%m-%d %H:%M:%S");
        oss << " GMT";

        return oss.str();
    }

    bool DBPingTimeManager::ExecuteQuery(const std::string& query)
    {
        std::cout << "[DB Query Placeholder] " << query << std::endl;

        // TODO: 실제 DB 쿼리 실행 구현 필요
        // if (mDbConnection) {
        //     return sqlite3_exec(mDbConnection, query.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
        // }

        return true;
    }

} // namespace Network::DBServer
