#pragma once

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IConnection.h"
#include "../Interfaces/IDatabase.h"
#include "../Interfaces/IResultSet.h"
#include "../Interfaces/IStatement.h"
#include "SqlScriptRunner.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Network::Database::SqlModuleBootstrap
{

struct ModuleSpec
{
    std::string moduleName;
    std::vector<std::string> tableScripts;
    std::vector<std::string> managedScripts;
};

namespace Detail
{

inline constexpr const char* kBootstrapStatusInProgress = "IN_PROGRESS";
inline constexpr const char* kBootstrapStatusCompleted = "COMPLETED";
inline constexpr const char* kBootstrapStatusFailed = "FAILED";

struct BootstrapState
{
    bool exists = false;
    std::string manifestHash;
    std::string status;
};

inline std::string ToLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

inline bool IsMissingTableError(const DatabaseException& exception)
{
    const std::string message = ToLowerCopy(exception.what());
    return message.find("no such table") != std::string::npos ||
           message.find("doesn't exist") != std::string::npos ||
           message.find("does not exist") != std::string::npos ||
           message.find("unknown table") != std::string::npos ||
           message.find("invalid object name") != std::string::npos ||
           message.find("base table or view not found") != std::string::npos ||
           message.find("table not found") != std::string::npos;
}

inline bool IsDuplicateKeyError(const DatabaseException& exception)
{
    const std::string message = ToLowerCopy(exception.what());
    return message.find("duplicate") != std::string::npos ||
           message.find("unique constraint failed") != std::string::npos ||
           message.find("primary key") != std::string::npos;
}

inline bool IsTableAlreadyExistsError(const DatabaseException& exception)
{
    const std::string message = ToLowerCopy(exception.what());
    return message.find("already exists") != std::string::npos ||
           message.find("already an object named") != std::string::npos ||
           message.find("table 't_modulebootstrapstate' already exists") !=
               std::string::npos ||
           message.find("relation \"t_modulebootstrapstate\" already exists") !=
               std::string::npos;
}

inline std::string CurrentUtcTimestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm utcTime {};
#ifdef _WIN32
    gmtime_s(&utcTime, &now);
#else
    gmtime_r(&now, &utcTime);
#endif

    std::ostringstream builder;
    builder << std::put_time(&utcTime, "%Y-%m-%d %H:%M:%S");
    return builder.str();
}

inline uint64_t AppendHash(uint64_t current, const std::string& value)
{
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    for (const unsigned char c : value)
    {
        current ^= static_cast<uint64_t>(c);
        current *= kFnvPrime;
    }

    current ^= 0xffull;
    current *= kFnvPrime;
    return current;
}

inline std::string ToHexString(uint64_t value)
{
    std::ostringstream builder;
    builder << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << value;
    return builder.str();
}

inline std::string ComputeManifestHash(const DatabaseConfig& config,
                                       const ModuleSpec& spec)
{
    uint64_t hash = 1469598103934665603ull;
    hash = AppendHash(hash, spec.moduleName);

    for (const std::string& scriptPath : spec.managedScripts)
    {
        hash = AppendHash(hash, scriptPath);
        hash = AppendHash(
            hash,
            SqlScriptRunner::LoadScript(spec.moduleName, scriptPath, config));
    }

    return ToHexString(hash);
}

inline std::unique_ptr<IConnection> CreateOpenConnection(IDatabase& database)
{
    auto connection = database.CreateConnection();
    if (!connection->IsOpen())
    {
        connection->Open(database.GetConfig().mConnectionString);
    }

    return connection;
}

inline void EnsureBootstrapStateTable(IConnection& connection)
{
    try
    {
        SqlScriptRunner::ExecuteRaw(
            connection,
            "CREATE TABLE T_ModuleBootstrapState ("
            "module_name VARCHAR(128) PRIMARY KEY,"
            "manifest_hash VARCHAR(64) NOT NULL,"
            "status VARCHAR(32) NOT NULL,"
            "created_at VARCHAR(32) NOT NULL,"
            "updated_at VARCHAR(32) NOT NULL"
            ")");
    }
    catch (const DatabaseException& exception)
    {
        if (!IsTableAlreadyExistsError(exception))
        {
            throw;
        }
    }
}

inline std::optional<BootstrapState> TryReadBootstrapState(IConnection& connection,
                                                           const std::string& moduleName)
{
    try
    {
        auto statement = SqlScriptRunner::PrepareRawStatement(
            connection,
            "SELECT manifest_hash, status "
            "FROM T_ModuleBootstrapState "
            "WHERE module_name = ?",
            [&](IStatement& stmt) { stmt.BindParameter(1, moduleName); });

        auto resultSet = statement->ExecuteQuery();
        BootstrapState state;
        if (resultSet->Next())
        {
            state.exists = true;
            state.manifestHash = resultSet->GetString("manifest_hash");
            state.status = resultSet->GetString("status");
        }

        return state;
    }
    catch (const DatabaseException& exception)
    {
        if (IsMissingTableError(exception))
        {
            return std::nullopt;
        }

        throw;
    }
}

inline void InsertBootstrapState(IConnection& connection,
                                 const std::string& moduleName,
                                 const std::string& manifestHash,
                                 const std::string& status,
                                 const std::string& timestamp)
{
    auto statement = SqlScriptRunner::PrepareRawStatement(
        connection,
        "INSERT INTO T_ModuleBootstrapState ("
        "module_name, manifest_hash, status, created_at, updated_at"
        ") VALUES ("
        "?, ?, ?, ?, ?"
        ")",
        [&](IStatement& stmt)
        {
            stmt.BindParameter(1, moduleName);
            stmt.BindParameter(2, manifestHash);
            stmt.BindParameter(3, status);
            stmt.BindParameter(4, timestamp);
            stmt.BindParameter(5, timestamp);
        });
    statement->ExecuteUpdate();
}

inline void UpdateBootstrapState(IConnection& connection,
                                 const std::string& moduleName,
                                 const std::string& manifestHash,
                                 const std::string& status,
                                 const std::string& timestamp)
{
    auto statement = SqlScriptRunner::PrepareRawStatement(
        connection,
        "UPDATE T_ModuleBootstrapState "
        "SET manifest_hash = ?, status = ?, updated_at = ? "
        "WHERE module_name = ?",
        [&](IStatement& stmt)
        {
            stmt.BindParameter(1, manifestHash);
            stmt.BindParameter(2, status);
            stmt.BindParameter(3, timestamp);
            stmt.BindParameter(4, moduleName);
        });
    statement->ExecuteUpdate();
}

inline void ValidateModuleSpec(const ModuleSpec& spec)
{
    if (spec.moduleName.empty())
    {
        throw DatabaseException("SQL module bootstrap requires a module name");
    }

    if (spec.tableScripts.empty())
    {
        throw DatabaseException("SQL module bootstrap requires at least one table script: " +
                                spec.moduleName);
    }

    if (spec.managedScripts.empty())
    {
        throw DatabaseException("SQL module bootstrap requires managed scripts: " +
                                spec.moduleName);
    }
}

inline void ExecuteBootstrapTables(IDatabase& database, const ModuleSpec& spec)
{
    for (const std::string& tableScript : spec.tableScripts)
    {
        SqlScriptRunner::Execute(database, spec.moduleName, tableScript);
    }
}

} // namespace Detail

inline bool BootstrapModuleIfNeeded(IDatabase& database, const ModuleSpec& spec)
{
    Detail::ValidateModuleSpec(spec);

    if (!database.IsConnected())
    {
        throw DatabaseException("Cannot bootstrap SQL module on a disconnected database: " +
                                spec.moduleName);
    }

    if (database.GetType() == DatabaseType::Mock)
    {
        Detail::ExecuteBootstrapTables(database, spec);
        return true;
    }

    const DatabaseConfig& config = database.GetConfig();
    const std::string manifestHash = Detail::ComputeManifestHash(config, spec);
    auto connection = Detail::CreateOpenConnection(database);

    std::optional<Detail::BootstrapState> state =
        Detail::TryReadBootstrapState(*connection, spec.moduleName);
    if (!state.has_value())
    {
        Detail::EnsureBootstrapStateTable(*connection);
        state = Detail::TryReadBootstrapState(*connection, spec.moduleName);
        if (!state.has_value())
        {
            throw DatabaseException("Failed to load bootstrap state table for module: " +
                                    spec.moduleName);
        }
    }

    if (state->exists)
    {
        if (state->status != Detail::kBootstrapStatusCompleted)
        {
            throw DatabaseException(
                "SQL bootstrap state is not completed for module " + spec.moduleName +
                " (status=" + state->status +
                "). Automatic patching is disabled after the first bootstrap.");
        }

        if (state->manifestHash != manifestHash)
        {
            throw DatabaseException(
                "SQL manifest changed for module " + spec.moduleName +
                ". Automatic patching is disabled after the first bootstrap.");
        }

        return false;
    }

    const std::string startedAt = Detail::CurrentUtcTimestamp();
    try
    {
        Detail::InsertBootstrapState(*connection, spec.moduleName, manifestHash,
                                     Detail::kBootstrapStatusInProgress, startedAt);
    }
    catch (const DatabaseException& exception)
    {
        if (!Detail::IsDuplicateKeyError(exception))
        {
            throw;
        }

        const std::optional<Detail::BootstrapState> racedState =
            Detail::TryReadBootstrapState(*connection, spec.moduleName);
        if (!racedState.has_value() || !racedState->exists)
        {
            throw;
        }

        if (racedState->status != Detail::kBootstrapStatusCompleted)
        {
            throw DatabaseException(
                "Concurrent SQL bootstrap detected for module " + spec.moduleName +
                " but the state is not completed (status=" + racedState->status + ").");
        }

        if (racedState->manifestHash != manifestHash)
        {
            throw DatabaseException(
                "Concurrent SQL bootstrap detected with a different manifest for module " +
                spec.moduleName + ".");
        }

        return false;
    }

    try
    {
        for (const std::string& tableScript : spec.tableScripts)
        {
            SqlScriptRunner::Execute(*connection, config, spec.moduleName, tableScript);
        }

        Detail::UpdateBootstrapState(*connection, spec.moduleName, manifestHash,
                                     Detail::kBootstrapStatusCompleted,
                                     Detail::CurrentUtcTimestamp());
        return true;
    }
    catch (...)
    {
        try
        {
            Detail::UpdateBootstrapState(*connection, spec.moduleName, manifestHash,
                                         Detail::kBootstrapStatusFailed,
                                         Detail::CurrentUtcTimestamp());
        }
        catch (...)
        {
        }

        throw;
    }
}

} // namespace Network::Database::SqlModuleBootstrap
