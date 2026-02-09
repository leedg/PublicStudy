// English: ServerLatencyManager implementation
// 한글: ServerLatencyManager 구현

#include "../include/ServerLatencyManager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

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

        // English: Create latency log table if not exists (placeholder)
        // 한글: 레이턴시 로그 테이블이 없으면 생성 (placeholder)
        std::string createTableQuery = R"(
            CREATE TABLE IF NOT EXISTS ServerLatencyLog (
                Id INTEGER PRIMARY KEY AUTOINCREMENT,
                ServerId INTEGER NOT NULL,
                ServerName VARCHAR(32),
                RttMs BIGINT NOT NULL,
                AvgRttMs DOUBLE NOT NULL,
                MinRttMs BIGINT NOT NULL,
                MaxRttMs BIGINT NOT NULL,
                PingCount BIGINT NOT NULL,
                MeasuredTimestamp BIGINT NOT NULL,
                MeasuredTimeGMT VARCHAR(32) NOT NULL,
                CreatedAt TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        )";

        // ExecuteQuery(createTableQuery);  // Placeholder call

        mInitialized.store(true, std::memory_order_release);
        Logger::Info("ServerLatencyManager initialized successfully");
        return true;
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

        // English: Persist to database (outside lock to minimize contention)
        // 한글: 데이터베이스에 저장 (경합 최소화를 위해 락 밖에서 실행)
        std::string query = BuildInsertQuery(
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

    std::string ServerLatencyManager::BuildInsertQuery(uint32_t serverId, const std::string& serverName,
                                                        uint64_t rttMs, double avgRttMs,
                                                        uint64_t minRttMs, uint64_t maxRttMs,
                                                        uint64_t pingCount, uint64_t timestamp)
    {
        std::string gmtTimeStr = FormatTimestamp(timestamp);

        std::ostringstream query;
        query << "INSERT INTO ServerLatencyLog "
              << "(ServerId, ServerName, RttMs, AvgRttMs, MinRttMs, MaxRttMs, PingCount, "
              << "MeasuredTimestamp, MeasuredTimeGMT) VALUES ("
              << serverId << ", '"
              << serverName << "', "
              << rttMs << ", "
              << std::fixed << std::setprecision(2) << avgRttMs << ", "
              << minRttMs << ", "
              << maxRttMs << ", "
              << pingCount << ", "
              << timestamp << ", '"
              << gmtTimeStr << "')";

        return query.str();
    }

    std::string ServerLatencyManager::FormatTimestamp(uint64_t timestamp)
    {
        // English: Convert milliseconds to seconds for time_t
        // 한글: 밀리초를 time_t용 초로 변환
        time_t seconds = static_cast<time_t>(timestamp / 1000);

        std::tm gmtTime;
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

    bool ServerLatencyManager::ExecuteQuery(const std::string& query)
    {
        // English: Placeholder for actual database query execution
        // 한글: 실제 데이터베이스 쿼리 실행을 위한 placeholder

        Logger::Debug("[DB Latency Query] " + query);

        // TODO: Implement actual database query execution
        return true;
    }

} // namespace Network::DBServer
