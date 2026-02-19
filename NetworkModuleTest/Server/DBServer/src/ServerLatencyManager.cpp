// English: ServerLatencyManager implementation
// 한글: ServerLatencyManager 구현

#include "../include/ServerLatencyManager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

// English: Include IDatabase / IStatement for real DB execution
// 한글: 실제 DB 실행을 위한 IDatabase / IStatement 포함
#ifdef _MSC_VER
// English: Suppress min/max macro collision with algorithm
// 한글: algorithm의 min/max 매크로 충돌 억제
#pragma warning(push)
#pragma warning(disable: 4005)
#endif
// English: Path resolves via include dirs that contain ServerEngine/
// 한글: ServerEngine/을 포함하는 include 경로로 해석
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

        // English: Create tables now only if a database was already injected before Initialize().
        //          If the database is injected after (via SetDatabase), EnsureTables() is called there.
        // 한글: Initialize() 이전에 DB가 이미 주입된 경우에만 지금 테이블 생성.
        //       이후에 SetDatabase()로 주입되면 거기서 EnsureTables() 호출.
        EnsureTables();

        mInitialized.store(true, std::memory_order_release);
        Logger::Info("ServerLatencyManager initialized successfully");
        return true;
    }

    void ServerLatencyManager::SetDatabase(Network::Database::IDatabase* db)
    {
        mDatabase = db;

        // English: If already initialized, ensure tables exist now that a DB is available.
        // 한글: 이미 초기화된 상태라면 DB가 주입된 지금 테이블을 보장한다.
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

        // English: Update in-memory latency stats (lock scope)
        // 한글: 메모리 내 레이턴시 통계 업데이트 (락 범위)
        {
            std::lock_guard<std::mutex> lock(mLatencyMutex);

            auto& info = mLatencyMap[serverId];

            // English: First time seeing this server
            // 한글: 이 서버를 처음 보는 경우
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
                // English: Update min/max
                // 한글: 최소/최대 업데이트
                info.minRttMs = std::min(info.minRttMs, rttMs);
                info.maxRttMs = std::max(info.maxRttMs, rttMs);

                // English: Incremental average: avg = avg + (new - avg) / count
                // 한글: 점진적 평균: avg = avg + (new - avg) / count
                info.avgRttMs = info.avgRttMs +
                    (static_cast<double>(rttMs) - info.avgRttMs) / static_cast<double>(info.pingCount + 1);
            }

            info.lastRttMs = rttMs;
            info.pingCount++;
            info.lastMeasuredTime = timestamp;

            // English: Copy for DB write outside lock
            // 한글: 락 밖에서 DB 저장을 위해 복사
            updatedInfo = info;
        }

        // English: Log latency measurement
        // 한글: 레이턴시 측정 로그
        Logger::Info("Latency recorded - ServerId: " + std::to_string(serverId) +
                     ", ServerName: " + serverName +
                     ", RTT: " + std::to_string(rttMs) + "ms" +
                     ", Avg: " + std::to_string(static_cast<uint64_t>(updatedInfo.avgRttMs)) + "ms" +
                     ", Min: " + std::to_string(updatedInfo.minRttMs) + "ms" +
                     ", Max: " + std::to_string(updatedInfo.maxRttMs) + "ms" +
                     ", Count: " + std::to_string(updatedInfo.pingCount));

        // English: Persist RTT stats to database (outside lock to minimize contention)
        // 한글: RTT 통계를 데이터베이스에 저장 (경합 최소화를 위해 락 밖에서 실행)
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

        // English: Update in-memory last-ping-time map (O(1) lookup for GetLastPingTime)
        // 한글: 메모리 내 마지막 Ping 시간 맵 갱신 (GetLastPingTime O(1) 조회용)
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
        // English: O(1) in-memory lookup — no DB round-trip needed
        // 한글: O(1) 메모리 조회 — DB 왕복 불필요
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
        // English: Column names match the CREATE TABLE in Initialize()
        // 한글: Initialize()의 CREATE TABLE 컬럼명과 일치
        std::ostringstream query;
        query << "INSERT INTO ServerLatencyLog "
              << "(server_id, server_name, rtt_ms, avg_rtt_ms, min_rtt_ms, max_rtt_ms, "
              << "ping_count, measured_time) VALUES ("
              << serverId << ", '"
              << serverName << "', "
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
        // English: Column names match the CREATE TABLE in Initialize()
        // 한글: Initialize()의 CREATE TABLE 컬럼명과 일치
        std::ostringstream query;
        query << "INSERT INTO PingTimeLog (server_id, server_name, ping_time) VALUES ("
              << serverId << ", '"
              << serverName << "', '"
              << FormatTimestamp(timestamp) << "')";
        return query.str();
    }

    std::string ServerLatencyManager::FormatTimestamp(uint64_t timestampMs) const
    {
        // English: Convert milliseconds → time_t seconds, then format as "YYYY-MM-DD HH:MM:SS GMT"
        // 한글: 밀리초 → time_t 초 변환 후 "YYYY-MM-DD HH:MM:SS GMT" 포맷
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
            // English: No database injected — log only
            // 한글: DB 미주입 — 로그만 출력
            return true;
        }

        try
        {
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
