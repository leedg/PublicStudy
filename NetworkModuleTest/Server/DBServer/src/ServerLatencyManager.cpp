// ServerLatencyManager implementation

#include "../include/ServerLatencyManager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

// Include IDatabase / IStatement for real DB execution
#ifdef _MSC_VER
// Suppress min/max macro collision with algorithm
#pragma warning(push)
#pragma warning(disable: 4005)
#endif
// Path resolves via include dirs that contain ServerEngine/
#include "../../ServerEngine/Interfaces/IDatabase.h"
#include "../../ServerEngine/Interfaces/IStatement.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Network::DBServer
{
    using namespace Network::Utils;

    ServerLatencyManager::ServerLatencyManager()
        : mInitialized{false}
    {
    }

    ServerLatencyManager::~ServerLatencyManager()
    {
        if (mInitialized)
        {
            Shutdown();
        }
    }

    bool ServerLatencyManager::Initialize()
    {
        if (mInitialized.load(std::memory_order_acquire))
        {
            Logger::Warn("ServerLatencyManager already initialized");
            return false;
        }

        Logger::Info("Initializing ServerLatencyManager...");

        // Create tables now only if a database was already injected before Initialize().
        //          If the database is injected after (via SetDatabase), EnsureTables() is called there.
        EnsureTables();

        mInitialized.store(true, std::memory_order_release);
        Logger::Info("ServerLatencyManager initialized successfully");
        return true;
    }

    void ServerLatencyManager::SetDatabase(Network::Database::IDatabase* db)
    {
        mDatabase = db;

        // If already initialized, ensure tables exist now that a DB is available.
        if (mInitialized.load(std::memory_order_acquire))
        {
            EnsureTables();
        }
    }

    void ServerLatencyManager::EnsureTables()
    {
        if (mDatabase == nullptr || !mDatabase->IsConnected())
            return;

        static const char* kCreateTableSQLs[] = {
            "CREATE TABLE IF NOT EXISTS ServerLatencyLog ("
            "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  server_id    INTEGER NOT NULL,"
            "  server_name  TEXT NOT NULL,"
            "  rtt_ms       INTEGER NOT NULL,"
            "  avg_rtt_ms   REAL,"
            "  min_rtt_ms   INTEGER,"
            "  max_rtt_ms   INTEGER,"
            "  ping_count   INTEGER,"
            "  measured_time TEXT NOT NULL"
            ")",
            "CREATE TABLE IF NOT EXISTS PingTimeLog ("
            "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  server_id   INTEGER NOT NULL,"
            "  server_name TEXT NOT NULL,"
            "  ping_time   TEXT NOT NULL"
            ")"
        };

        for (const char* sql : kCreateTableSQLs)
        {
            try
            {
                auto stmt = mDatabase->CreateStatement();
                stmt->SetQuery(sql);
                stmt->Execute();
            }
            catch (const std::exception& e)
            {
                Logger::Warn("ServerLatencyManager: Failed to create table: " +
                             std::string(e.what()));
            }
        }
        Logger::Info("ServerLatencyManager: DB tables ensured (ServerLatencyLog, PingTimeLog)");
    }

    void ServerLatencyManager::Shutdown()
    {
        if (!mInitialized.load(std::memory_order_acquire))
            return;

        Logger::Info("Shutting down ServerLatencyManager...");
        mInitialized.store(false, std::memory_order_release);
        Logger::Info("ServerLatencyManager shut down");
    }

    void ServerLatencyManager::RecordLatency(uint32_t serverId, const std::string& serverName,
                                              uint64_t rttMs, uint64_t timestamp)
    {
        if (!mInitialized.load(std::memory_order_acquire))
        {
            Logger::Error("ServerLatencyManager not initialized");
            return;
        }

        ServerLatencyInfo updatedInfo;

        // Update in-memory latency stats (lock scope)
        {
            std::lock_guard<std::mutex> lock(mLatencyMutex);

            auto& info = mLatencyMap[serverId];

            // First time seeing this server
            if (info.pingCount == 0)
            {
                info.serverId = serverId;
                info.serverName = serverName;
                info.minRttMs = rttMs;
                info.maxRttMs = rttMs;
                info.avgRttMs = static_cast<double>(rttMs);
            }
            else
            {
                // Update min/max
                info.minRttMs = std::min(info.minRttMs, rttMs);
                info.maxRttMs = std::max(info.maxRttMs, rttMs);

                // Incremental average: avg = avg + (new - avg) / count
                info.avgRttMs = info.avgRttMs +
                    (static_cast<double>(rttMs) - info.avgRttMs) / static_cast<double>(info.pingCount + 1);
            }

            info.lastRttMs = rttMs;
            info.pingCount++;
            info.lastMeasuredTime = timestamp;

            // Copy for DB write outside lock
            updatedInfo = info;
        }

        // Log latency measurement
        Logger::Info("Latency recorded - ServerId: " + std::to_string(serverId) +
                     ", ServerName: " + serverName +
                     ", RTT: " + std::to_string(rttMs) + "ms" +
                     ", Avg: " + std::to_string(static_cast<uint64_t>(updatedInfo.avgRttMs)) + "ms" +
                     ", Min: " + std::to_string(updatedInfo.minRttMs) + "ms" +
                     ", Max: " + std::to_string(updatedInfo.maxRttMs) + "ms" +
                     ", Count: " + std::to_string(updatedInfo.pingCount));

        // Persist RTT stats to database (outside lock to minimize contention)
        const std::string query = BuildLatencyInsertQuery(
            serverId, serverName, rttMs,
            updatedInfo.avgRttMs, updatedInfo.minRttMs, updatedInfo.maxRttMs,
            updatedInfo.pingCount, timestamp);

        ExecuteQuery(query);
    }

    bool ServerLatencyManager::GetLatencyInfo(uint32_t serverId, ServerLatencyInfo& outInfo) const
    {
        std::lock_guard<std::mutex> lock(mLatencyMutex);

        auto it = mLatencyMap.find(serverId);
        if (it == mLatencyMap.end())
            return false;

        outInfo = it->second;
        return true;
    }

    std::unordered_map<uint32_t, ServerLatencyInfo> ServerLatencyManager::GetAllLatencyInfos() const
    {
        std::lock_guard<std::mutex> lock(mLatencyMutex);
        return mLatencyMap;  // Copy
    }

    // ── Ping timestamp (merged from DBPingTimeManager) ───────────────────────

    bool ServerLatencyManager::SavePingTime(uint32_t serverId,
                                             const std::string& serverName,
                                             uint64_t timestamp)
    {
        if (!mInitialized.load(std::memory_order_acquire))
        {
            Logger::Error("ServerLatencyManager::SavePingTime — not initialized");
            return false;
        }

        // Update in-memory last-ping-time map (O(1) lookup for GetLastPingTime)
        {
            std::lock_guard<std::mutex> lock(mPingTimeMutex);
            mLastPingTimeMap[serverId] = timestamp;
        }

        const std::string query = BuildPingTimeInsertQuery(serverId, serverName, timestamp);

        Logger::Debug("SavePingTime - ServerId: " + std::to_string(serverId) +
                      ", ServerName: " + serverName +
                      ", GMT: " + FormatTimestamp(timestamp));

        return ExecuteQuery(query);
    }

    uint64_t ServerLatencyManager::GetLastPingTime(uint32_t serverId) const
    {
        // O(1) in-memory lookup — no DB round-trip needed
        std::lock_guard<std::mutex> lock(mPingTimeMutex);
        auto it = mLastPingTimeMap.find(serverId);
        return (it != mLastPingTimeMap.end()) ? it->second : 0;
    }

    // ── Private helpers ───────────────────────────────────────────────────────

    std::string ServerLatencyManager::BuildLatencyInsertQuery(uint32_t serverId,
                                                               const std::string& serverName,
                                                               uint64_t rttMs, double avgRttMs,
                                                               uint64_t minRttMs, uint64_t maxRttMs,
                                                               uint64_t pingCount, uint64_t timestamp)
    {
        // Column names match the CREATE TABLE in Initialize()
        std::ostringstream query;
        const std::string safeServerName = EscapeSqlLiteral(serverName);
        query << "INSERT INTO ServerLatencyLog "
              << "(server_id, server_name, rtt_ms, avg_rtt_ms, min_rtt_ms, max_rtt_ms, "
              << "ping_count, measured_time) VALUES ("
              << serverId << ", '"
              << safeServerName << "', "
              << rttMs << ", "
              << std::fixed << std::setprecision(2) << avgRttMs << ", "
              << minRttMs << ", "
              << maxRttMs << ", "
              << pingCount << ", '"
              << FormatTimestamp(timestamp) << "')";
        return query.str();
    }

    std::string ServerLatencyManager::BuildPingTimeInsertQuery(uint32_t serverId,
                                                                const std::string& serverName,
                                                                uint64_t timestamp)
    {
        // Column names match the CREATE TABLE in Initialize()
        std::ostringstream query;
        const std::string safeServerName = EscapeSqlLiteral(serverName);
        query << "INSERT INTO PingTimeLog (server_id, server_name, ping_time) VALUES ("
              << serverId << ", '"
              << safeServerName << "', '"
              << FormatTimestamp(timestamp) << "')";
        return query.str();
    }

    // Escapes SQL string literal by doubling single quotes.
    std::string ServerLatencyManager::EscapeSqlLiteral(const std::string& value)
    {
        std::string escaped;
        escaped.reserve(value.size());

        for (char ch : value)
        {
            if (ch == '\'')
            {
                escaped += "''";
            }
            else
            {
                escaped.push_back(ch);
            }
        }

        return escaped;
    }

    std::string ServerLatencyManager::FormatTimestamp(uint64_t timestampMs) const
    {
        // Convert milliseconds → time_t seconds, then format as "YYYY-MM-DD HH:MM:SS GMT"
        time_t seconds = static_cast<time_t>(timestampMs / 1000);

        std::tm gmtTime{};
#ifdef _WIN32
        gmtime_s(&gmtTime, &seconds);
#else
        gmtime_r(&seconds, &gmtTime);
#endif

        std::ostringstream oss;
        oss << std::put_time(&gmtTime, "%Y-%m-%d %H:%M:%S") << " GMT";
        return oss.str();
    }

    bool ServerLatencyManager::ExecuteQuery(const std::string& query)
    {
        Logger::Debug("[DB Query] " + query);

        if (mDatabase == nullptr || !mDatabase->IsConnected())
        {
            // No database injected — log only
            return true;
        }

        try
        {
            // Double-check connection state before query execution
            if (!mDatabase->IsConnected())
            {
                Logger::Warn("ServerLatencyManager: Database connection lost during query execution");
                return false;
            }

            auto stmt = mDatabase->CreateStatement();
            stmt->SetQuery(query);
            stmt->ExecuteUpdate();
            return true;
        }
        catch (const std::exception& e)
        {
            Logger::Error("ServerLatencyManager ExecuteQuery failed: " + std::string(e.what()));
            return false;
        }
    }

} // namespace Network::DBServer
