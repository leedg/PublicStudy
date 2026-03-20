// English: ServerLatencyManager implementation

#include "../include/ServerLatencyManager.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4005)
#endif
#include "../../ServerEngine/Database/SqlModuleBootstrap.h"
#include "../../ServerEngine/Database/SqlScriptRunner.h"
#include "../../ServerEngine/Interfaces/IDatabase.h"
#include "../../ServerEngine/Interfaces/IStatement.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <vector>

namespace Network::DBServer
{
    using namespace Network::Utils;

    namespace
    {
        constexpr const char* kSqlModuleName = "DBServer";

        const Network::Database::SqlModuleBootstrap::ModuleSpec& GetDBServerSqlModuleSpec()
        {
            using Network::Database::SqlModuleBootstrap::ModuleSpec;

            static const ModuleSpec spec = [] {
                ModuleSpec value;
                value.moduleName = kSqlModuleName;
                value.tableScripts = {
                    "TABLE/T_ServerLatencyLog.sql",
                    "TABLE/T_PingTimeLog.sql"
                };
                value.managedScripts = value.tableScripts;
                value.managedScripts.insert(
                    value.managedScripts.end(),
                    {
                        "SP/SP_InsertServerLatencyLog.sql",
                        "SP/SP_InsertPingTimeLog.sql"
                    });
                return value;
            }();

            return spec;
        }

        template <typename Binder = std::nullptr_t>
        bool ExecuteModuleScript(Network::Database::IDatabase& database,
                                 const char* relativePath,
                                 Binder&& binder = nullptr)
        {
            return Network::Database::SqlScriptRunner::Execute(
                database, kSqlModuleName, relativePath, std::forward<Binder>(binder));
        }

        template <typename Binder = std::nullptr_t>
        int ExecuteModuleScriptUpdate(Network::Database::IDatabase& database,
                                      const char* relativePath,
                                      Binder&& binder = nullptr)
        {
            return Network::Database::SqlScriptRunner::ExecuteUpdate(
                database, kSqlModuleName, relativePath, std::forward<Binder>(binder));
        }
    }

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
        try
        {
            EnsureTables();
        }
        catch (const std::exception& e)
        {
            Logger::Error("ServerLatencyManager bootstrap failed: " +
                          std::string(e.what()));
            return false;
        }

        mInitialized.store(true, std::memory_order_release);
        Logger::Info("ServerLatencyManager initialized successfully");
        return true;
    }

    void ServerLatencyManager::SetDatabase(Network::Database::IDatabase* db)
    {
        mDatabase = db;

        if (mInitialized.load(std::memory_order_acquire))
        {
            try
            {
                EnsureTables();
            }
            catch (const std::exception& e)
            {
                Logger::Error("ServerLatencyManager bootstrap failed on SetDatabase: " +
                              std::string(e.what()));
            }
        }
    }

    void ServerLatencyManager::EnsureTables()
    {
        if (mDatabase == nullptr || !mDatabase->IsConnected())
        {
            return;
        }

        const bool bootstrapped =
            Network::Database::SqlModuleBootstrap::BootstrapModuleIfNeeded(
                *mDatabase,
                GetDBServerSqlModuleSpec());
        Logger::Info(bootstrapped
                         ? "ServerLatencyManager: initial SQL bootstrap completed"
                         : "ServerLatencyManager: SQL manifest verified");
    }

    void ServerLatencyManager::Shutdown()
    {
        if (!mInitialized.load(std::memory_order_acquire))
        {
            return;
        }

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

        {
            std::lock_guard<std::mutex> lock(mLatencyMutex);

            auto& info = mLatencyMap[serverId];
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
                info.minRttMs = std::min(info.minRttMs, rttMs);
                info.maxRttMs = std::max(info.maxRttMs, rttMs);
                info.avgRttMs = info.avgRttMs +
                    (static_cast<double>(rttMs) - info.avgRttMs) /
                    static_cast<double>(info.pingCount + 1);
            }

            info.lastRttMs = rttMs;
            info.pingCount++;
            info.lastMeasuredTime = timestamp;
            updatedInfo = info;
        }

        Logger::Info("Latency recorded - ServerId: " + std::to_string(serverId) +
                     ", ServerName: " + serverName +
                     ", RTT: " + std::to_string(rttMs) + "ms" +
                     ", Avg: " + std::to_string(static_cast<uint64_t>(updatedInfo.avgRttMs)) + "ms" +
                     ", Min: " + std::to_string(updatedInfo.minRttMs) + "ms" +
                     ", Max: " + std::to_string(updatedInfo.maxRttMs) + "ms" +
                     ", Count: " + std::to_string(updatedInfo.pingCount));

        if (mDatabase == nullptr || !mDatabase->IsConnected())
        {
            return;
        }

        const std::string measuredTime = FormatTimestamp(timestamp);

        try
        {
            ExecuteModuleScriptUpdate(
                *mDatabase,
                "SP/SP_InsertServerLatencyLog.sql",
                [&](Network::Database::IStatement& stmt)
                {
                    stmt.BindParameter(1, static_cast<long long>(serverId));
                    stmt.BindParameter(2, serverName);
                    stmt.BindParameter(3, static_cast<long long>(rttMs));
                    stmt.BindParameter(4, updatedInfo.avgRttMs);
                    stmt.BindParameter(5, static_cast<long long>(updatedInfo.minRttMs));
                    stmt.BindParameter(6, static_cast<long long>(updatedInfo.maxRttMs));
                    stmt.BindParameter(7, static_cast<long long>(updatedInfo.pingCount));
                    stmt.BindParameter(8, measuredTime);
                });
        }
        catch (const std::exception& e)
        {
            Logger::Error("ServerLatencyManager latency persist failed: " +
                          std::string(e.what()));
        }
    }

    bool ServerLatencyManager::GetLatencyInfo(uint32_t serverId, ServerLatencyInfo& outInfo) const
    {
        std::lock_guard<std::mutex> lock(mLatencyMutex);

        auto it = mLatencyMap.find(serverId);
        if (it == mLatencyMap.end())
        {
            return false;
        }

        outInfo = it->second;
        return true;
    }

    std::unordered_map<uint32_t, ServerLatencyInfo> ServerLatencyManager::GetAllLatencyInfos() const
    {
        std::lock_guard<std::mutex> lock(mLatencyMutex);
        return mLatencyMap;
    }

    bool ServerLatencyManager::SavePingTime(uint32_t serverId,
                                            const std::string& serverName,
                                            uint64_t timestamp)
    {
        if (!mInitialized.load(std::memory_order_acquire))
        {
            Logger::Error("ServerLatencyManager::SavePingTime not initialized");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mPingTimeMutex);
            mLastPingTimeMap[serverId] = timestamp;
        }

        const std::string pingTime = FormatTimestamp(timestamp);
        Logger::Debug("SavePingTime - ServerId: " + std::to_string(serverId) +
                      ", ServerName: " + serverName +
                      ", GMT: " + pingTime);

        if (mDatabase == nullptr || !mDatabase->IsConnected())
        {
            return true;
        }

        try
        {
            ExecuteModuleScriptUpdate(
                *mDatabase,
                "SP/SP_InsertPingTimeLog.sql",
                [&](Network::Database::IStatement& stmt)
                {
                    stmt.BindParameter(1, static_cast<long long>(serverId));
                    stmt.BindParameter(2, serverName);
                    stmt.BindParameter(3, pingTime);
                });
            return true;
        }
        catch (const std::exception& e)
        {
            Logger::Error("ServerLatencyManager ping persist failed: " +
                          std::string(e.what()));
            return false;
        }
    }

    uint64_t ServerLatencyManager::GetLastPingTime(uint32_t serverId) const
    {
        std::lock_guard<std::mutex> lock(mPingTimeMutex);
        auto it = mLastPingTimeMap.find(serverId);
        return (it != mLastPingTimeMap.end()) ? it->second : 0;
    }

    std::string ServerLatencyManager::FormatTimestamp(uint64_t timestampMs) const
    {
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

} // namespace Network::DBServer
