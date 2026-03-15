#pragma once

// =============================================================================
// DEPRECATED — DBPingTimeManager has been merged into ServerLatencyManager.
//
//   All functionality previously provided here is now available through
//   ServerLatencyManager:
//     SavePingTime(serverId, serverName, timestamp)  ← was DBPingTimeManager::SavePingTime
//     GetLastPingTime(serverId)                      ← was DBPingTimeManager::GetLastPingTime
//
//   This file is retained temporarily to avoid breaking any external include
//   sites that have not yet been updated.  It will be removed in a future cleanup.
//   Do NOT add new dependencies on this class.
//
//
//
// =============================================================================

// DB Ping Time Manager - handles ping timestamp storage

#include <cstdint>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>

namespace Network::DBServer
{
    // =============================================================================
    // DBPingTimeManager - manages ping timestamp storage in database
    // =============================================================================

    class DBPingTimeManager
    {
    public:
        DBPingTimeManager();
        ~DBPingTimeManager();

        // Initialize the manager
        bool Initialize();

        // Shutdown the manager
        void Shutdown();

        // Save ping timestamp to database (GMT)
        // @param serverId - Server identifier
        // @param serverName - Server name
        // @param timestamp - Ping timestamp in milliseconds since epoch (GMT)
        // @return true if save succeeded, false otherwise
        bool SavePingTime(uint32_t serverId, const std::string& serverName, uint64_t timestamp);

        // Get last ping timestamp for a server
        // @param serverId - Server identifier
        // @return timestamp in milliseconds, or 0 if not found
        uint64_t GetLastPingTime(uint32_t serverId);

        // Check if manager is initialized
        bool IsInitialized() const { return mInitialized.load(std::memory_order_acquire); }

    private:
        // Internal helper to format GMT timestamp as string
        std::string FormatTimestamp(uint64_t timestamp);

        // Execute actual database query (placeholder for real implementation)
        bool ExecuteQuery(const std::string& query);

    private:
        std::atomic<bool> mInitialized;
        std::mutex mMutex;

        // Database connection placeholder
        // TODO: Add actual database connection here
        // void* mDbConnection;
    };

} // namespace Network::DBServer
