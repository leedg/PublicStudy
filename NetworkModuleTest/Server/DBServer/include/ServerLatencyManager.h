#pragma once

// Forward-declare IDatabase to avoid pulling in ServerEngine headers
namespace Network { namespace Database { class IDatabase; } }

// ServerLatencyManager - unified per-server latency tracker and ping time recorder.
//
//   Replaces the two separate classes that were previously responsible for these concerns:
//     - ServerLatencyManager  : RTT statistics (min / max / avg) + ServerLatencyLog persistence
//     - DBPingTimeManager     : ping timestamp storage + PingTimeLog persistence  ← MERGED IN
//
//   Both managers wrote to different DB tables but shared identical FormatTimestamp /
//   ExecuteQuery infrastructure, and ServerPacketHandler had to coordinate both in every
//   async task.  Merging eliminates the duplication and halves the dependency list.
//
//
//

#include "Utils/NetworkUtils.h"
#include <cstdint>
#include <string>
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace Network::DBServer
{
    // =============================================================================
    // Per-server latency statistics
    // =============================================================================

    struct ServerLatencyInfo
    {
        uint32_t serverId = 0;
        std::string serverName;

        // Latest RTT measurement (ms)
        uint64_t lastRttMs = 0;

        // Running average RTT (ms)
        double avgRttMs = 0.0;

        // Min/Max RTT (ms)
        uint64_t minRttMs = UINT64_MAX;
        uint64_t maxRttMs = 0;

        // Total ping count for this server
        uint64_t pingCount = 0;

        // Timestamp of last measurement
        uint64_t lastMeasuredTime = 0;
    };

    // =============================================================================
    // ServerLatencyManager - per-server latency tracker
    // =============================================================================

    class ServerLatencyManager
    {
    public:
        ServerLatencyManager();
        ~ServerLatencyManager();

        // Initialize the manager
        bool Initialize();

        // Shutdown the manager
        void Shutdown();

        // ── RTT statistics ──────────────────────────────────────────────────────

        // Record a latency measurement for a server.
        //          Updates in-memory RTT stats and persists to ServerLatencyLog.
        // @param serverId    - Server identifier (from PKT_ServerPingReq)
        // @param serverName  - Human-readable server name
        // @param rttMs       - Round-trip time in milliseconds
        // @param timestamp   - Measurement timestamp (ms since epoch, GMT)
        void RecordLatency(uint32_t serverId, const std::string& serverName,
                           uint64_t rttMs, uint64_t timestamp);

        // Get latency info for a specific server (thread-safe copy)
        bool GetLatencyInfo(uint32_t serverId, ServerLatencyInfo& outInfo) const;

        // Get all server latency infos (thread-safe snapshot)
        std::unordered_map<uint32_t, ServerLatencyInfo> GetAllLatencyInfos() const;

        // ── Ping timestamp (merged from DBPingTimeManager) ───────────────────

        // Persist a ping timestamp to PingTimeLog for a server.
        //          Previously handled by DBPingTimeManager::SavePingTime.
        // @param serverId   - Server identifier
        // @param serverName - Human-readable server name
        // @param timestamp  - Ping timestamp in milliseconds since epoch (GMT)
        // @return true if the write succeeded
        bool SavePingTime(uint32_t serverId, const std::string& serverName,
                          uint64_t timestamp);

        // Return the last ping timestamp for a server (in-memory, O(1)).
        //          Returns 0 if the server has never been seen.
        //          Previously handled by DBPingTimeManager::GetLastPingTime.
        uint64_t GetLastPingTime(uint32_t serverId) const;

        bool IsInitialized() const { return mInitialized.load(std::memory_order_acquire); }

        // Inject a database connection for persistent storage (non-owning)
        void SetDatabase(Network::Database::IDatabase* db);

    private:
        // Format latency data as SQL INSERT for ServerLatencyLog
        std::string BuildLatencyInsertQuery(uint32_t serverId, const std::string& serverName,
                                            uint64_t rttMs, double avgRttMs,
                                            uint64_t minRttMs, uint64_t maxRttMs,
                                            uint64_t pingCount, uint64_t timestamp);

        // Format ping data as SQL INSERT for PingTimeLog (merged from DBPingTimeManager)
        std::string BuildPingTimeInsertQuery(uint32_t serverId, const std::string& serverName,
                                             uint64_t timestamp);

        // Escape SQL literal by doubling single quotes.
        static std::string EscapeSqlLiteral(const std::string& value);

        // Format millisecond timestamp as "YYYY-MM-DD HH:MM:SS GMT" string
        std::string FormatTimestamp(uint64_t timestampMs) const;

        // Execute a database query
        bool ExecuteQuery(const std::string& query);

        // Create persistent tables if a live database is available.
        //          Called from both Initialize() and SetDatabase() so that tables are
        //          always created regardless of injection order.
        void EnsureTables();

    private:
        std::atomic<bool> mInitialized;

        // Injected database (non-owning); nullptr = log-only mode
        Network::Database::IDatabase* mDatabase = nullptr;

        // Per-server latency map, guarded by mutex
        mutable std::mutex mLatencyMutex;
        std::unordered_map<uint32_t, ServerLatencyInfo> mLatencyMap;

        // Last ping timestamp per server (for GetLastPingTime O(1))
        mutable std::mutex mPingTimeMutex;
        std::unordered_map<uint32_t, uint64_t> mLastPingTimeMap;
    };

} // namespace Network::DBServer
