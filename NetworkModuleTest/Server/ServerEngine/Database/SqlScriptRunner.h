#pragma once

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IStatement.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace Network::Database::SqlScriptRunner
{
namespace Detail
{

inline std::string ToLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

inline std::optional<SqlDialect> DetectDialect(DatabaseType type)
{
    switch (type)
    {
    case DatabaseType::OLEDB:
        return SqlDialect::SQLServer;

    case DatabaseType::SQLite:
        return SqlDialect::SQLite;

    case DatabaseType::MySQL:
        return SqlDialect::MySQL;

    case DatabaseType::PostgreSQL:
        return SqlDialect::PostgreSQL;

    default:
        return std::nullopt;
    }
}

inline std::optional<SqlDialect> DetectDialectFromText(const std::string& text)
{
    const std::string lowered = ToLowerCopy(text);
    if (lowered.find("mysql") != std::string::npos ||
        lowered.find("mariadb") != std::string::npos)
    {
        return SqlDialect::MySQL;
    }

    if (lowered.find("sqlite") != std::string::npos)
    {
        return SqlDialect::SQLite;
    }

    if (lowered.find("postgres") != std::string::npos ||
        lowered.find("psqlodbc") != std::string::npos)
    {
        return SqlDialect::PostgreSQL;
    }

    if (lowered.find("sql server") != std::string::npos ||
        lowered.find("mssql") != std::string::npos ||
        lowered.find("sqloledb") != std::string::npos ||
        lowered.find("msodbcsql") != std::string::npos ||
        lowered.find("native client") != std::string::npos)
    {
        return SqlDialect::SQLServer;
    }

    return std::nullopt;
}

inline SqlDialect DetectDialect(const DatabaseConfig& config)
{
    if (config.mSqlDialectHint != SqlDialect::Auto)
    {
        return config.mSqlDialectHint;
    }

    if (const auto backendDialect = DetectDialect(config.mType);
        backendDialect.has_value())
    {
        return *backendDialect;
    }

    if (const auto connectionDialect =
            DetectDialectFromText(config.mConnectionString);
        connectionDialect.has_value())
    {
        return *connectionDialect;
    }

    return SqlDialect::Generic;
}

inline const char* GetDialectDirectoryName(SqlDialect dialect)
{
    switch (dialect)
    {
    case SqlDialect::SQLite:
        return "SQLite";

    case SqlDialect::MySQL:
        return "MySQL";

    case SqlDialect::PostgreSQL:
        return "PostgreSQL";

    case SqlDialect::SQLServer:
        return "SQLServer";

    default:
        return nullptr;
    }
}

inline std::filesystem::path BuildVariantRelativePath(const std::string& relativePath,
                                                      const DatabaseConfig* config)
{
    if (config == nullptr)
    {
        return {};
    }

    const char* dialectDirectory = GetDialectDirectoryName(DetectDialect(*config));
    if (dialectDirectory == nullptr)
    {
        return {};
    }

    const std::filesystem::path source(relativePath);
    const std::filesystem::path parent = source.parent_path();
    if (parent.empty())
    {
        return std::filesystem::path(dialectDirectory) / source.filename();
    }

    return parent / dialectDirectory / source.filename();
}

inline std::optional<std::filesystem::path>
TryResolveScriptPath(const std::string& moduleName,
                     const std::string& relativePath)
{
    namespace fs = std::filesystem;

    const fs::path modulePath(moduleName);
    const fs::path dbRelativePath = fs::path("DB") / fs::path(relativePath);

    fs::path cursor = fs::current_path();
    for (int depth = 0; depth < 6; ++depth)
    {
        const fs::path directCandidate = cursor / modulePath / dbRelativePath;
        if (fs::exists(directCandidate) && fs::is_regular_file(directCandidate))
        {
            return directCandidate;
        }

        const fs::path serverCandidate = cursor / "Server" / modulePath / dbRelativePath;
        if (fs::exists(serverCandidate) && fs::is_regular_file(serverCandidate))
        {
            return serverCandidate;
        }

        if (!cursor.has_parent_path())
        {
            break;
        }

        const fs::path parent = cursor.parent_path();
        if (parent == cursor)
        {
            break;
        }

        cursor = parent;
    }

    return std::nullopt;
}

inline std::filesystem::path ResolveScriptPath(const std::string& moduleName,
                                               const std::string& relativePath,
                                               const DatabaseConfig* config = nullptr)
{
    if (const std::filesystem::path variantPath =
            BuildVariantRelativePath(relativePath, config);
        !variantPath.empty())
    {
        if (const auto resolvedVariant =
                TryResolveScriptPath(moduleName, variantPath.generic_string());
            resolvedVariant.has_value())
        {
            return *resolvedVariant;
        }
    }

    if (const auto resolvedDefault = TryResolveScriptPath(moduleName, relativePath);
        resolvedDefault.has_value())
    {
        return *resolvedDefault;
    }

    throw DatabaseException("SQL script not found: module=" + moduleName +
                            ", path=" + relativePath +
                            ", cwd=" + std::filesystem::current_path().string());
}

inline std::string ReadScriptText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        throw DatabaseException("Failed to open SQL script: " + path.string());
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    std::string text = buffer.str();
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
    {
        text.erase(0, 3);
    }

    if (text.empty())
    {
        throw DatabaseException("SQL script is empty: " + path.string());
    }

    return text;
}

template <typename T, typename = void>
struct HasGetConfig : std::false_type
{
};

template <typename T>
struct HasGetConfig<T, std::void_t<decltype(std::declval<const T&>().GetConfig())>>
    : std::true_type
{
};

template <typename T>
inline const DatabaseConfig* TryGetConfig(const T& owner)
{
    if constexpr (HasGetConfig<T>::value)
    {
        return &owner.GetConfig();
    }

    return nullptr;
}

template <typename Binder>
inline void ApplyBinder(IStatement& stmt, Binder&& binder)
{
    if constexpr (!std::is_same_v<std::decay_t<Binder>, std::nullptr_t>)
    {
        binder(stmt);
    }
}

template <typename StatementOwner, typename Binder>
inline std::unique_ptr<IStatement>
PrepareStatementWithQuery(StatementOwner& owner,
                          const std::string& queryText,
                          Binder&& binder)
{
    auto stmt = owner.CreateStatement();
    stmt->SetQuery(queryText);
    ApplyBinder(*stmt, std::forward<Binder>(binder));
    return stmt;
}

} // namespace Detail

inline SqlDialect InferSqlDialectHint(const std::string& connectionString)
{
    if (const auto dialect = Detail::DetectDialectFromText(connectionString);
        dialect.has_value())
    {
        return *dialect;
    }

    return SqlDialect::Auto;
}

inline std::filesystem::path ResolvePath(const std::string& moduleName,
                                         const std::string& relativePath)
{
    return Detail::ResolveScriptPath(moduleName, relativePath);
}

inline std::filesystem::path ResolvePath(const std::string& moduleName,
                                         const std::string& relativePath,
                                         const DatabaseConfig& config)
{
    return Detail::ResolveScriptPath(moduleName, relativePath, &config);
}

inline std::string LoadScript(const std::string& moduleName,
                              const std::string& relativePath)
{
    return Detail::ReadScriptText(ResolvePath(moduleName, relativePath));
}

inline std::string LoadScript(const std::string& moduleName,
                              const std::string& relativePath,
                              const DatabaseConfig& config)
{
    return Detail::ReadScriptText(ResolvePath(moduleName, relativePath, config));
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline std::unique_ptr<IStatement>
PrepareStatement(StatementOwner& owner,
                 const std::string& moduleName,
                 const std::string& relativePath,
                 Binder&& binder = nullptr)
{
    return Detail::PrepareStatementWithQuery(
        owner,
        Detail::ReadScriptText(Detail::ResolveScriptPath(
            moduleName, relativePath, Detail::TryGetConfig(owner))),
        std::forward<Binder>(binder));
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline std::unique_ptr<IStatement>
PrepareStatement(StatementOwner& owner,
                 const DatabaseConfig& config,
                 const std::string& moduleName,
                 const std::string& relativePath,
                 Binder&& binder = nullptr)
{
    return Detail::PrepareStatementWithQuery(
        owner, LoadScript(moduleName, relativePath, config),
        std::forward<Binder>(binder));
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline std::unique_ptr<IStatement>
PrepareRawStatement(StatementOwner& owner,
                    const std::string& queryText,
                    Binder&& binder = nullptr)
{
    return Detail::PrepareStatementWithQuery(owner, queryText,
                                             std::forward<Binder>(binder));
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline std::unique_ptr<IStatement>
PrepareRawFileStatement(StatementOwner& owner,
                        const std::string& moduleName,
                        const std::string& relativePath,
                        Binder&& binder = nullptr)
{
    return PrepareStatement(owner, moduleName, relativePath,
                            std::forward<Binder>(binder));
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline std::unique_ptr<IStatement>
PrepareRawFileStatement(StatementOwner& owner,
                        const DatabaseConfig& config,
                        const std::string& moduleName,
                        const std::string& relativePath,
                        Binder&& binder = nullptr)
{
    return PrepareStatement(owner, config, moduleName, relativePath,
                            std::forward<Binder>(binder));
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline bool Execute(StatementOwner& owner,
                    const std::string& moduleName,
                    const std::string& relativePath,
                    Binder&& binder = nullptr)
{
    auto stmt = PrepareStatement(owner, moduleName, relativePath,
                                 std::forward<Binder>(binder));
    return stmt->Execute();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline bool Execute(StatementOwner& owner,
                    const DatabaseConfig& config,
                    const std::string& moduleName,
                    const std::string& relativePath,
                    Binder&& binder = nullptr)
{
    auto stmt = PrepareStatement(owner, config, moduleName, relativePath,
                                 std::forward<Binder>(binder));
    return stmt->Execute();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline int ExecuteUpdate(StatementOwner& owner,
                         const std::string& moduleName,
                         const std::string& relativePath,
                         Binder&& binder = nullptr)
{
    auto stmt = PrepareStatement(owner, moduleName, relativePath,
                                 std::forward<Binder>(binder));
    return stmt->ExecuteUpdate();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline int ExecuteUpdate(StatementOwner& owner,
                         const DatabaseConfig& config,
                         const std::string& moduleName,
                         const std::string& relativePath,
                         Binder&& binder = nullptr)
{
    auto stmt = PrepareStatement(owner, config, moduleName, relativePath,
                                 std::forward<Binder>(binder));
    return stmt->ExecuteUpdate();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline std::unique_ptr<IResultSet>
ExecuteQuery(StatementOwner& owner,
             const std::string& moduleName,
             const std::string& relativePath,
             Binder&& binder = nullptr)
{
    auto stmt = PrepareStatement(owner, moduleName, relativePath,
                                 std::forward<Binder>(binder));
    return stmt->ExecuteQuery();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline std::unique_ptr<IResultSet>
ExecuteQuery(StatementOwner& owner,
             const DatabaseConfig& config,
             const std::string& moduleName,
             const std::string& relativePath,
             Binder&& binder = nullptr)
{
    auto stmt = PrepareStatement(owner, config, moduleName, relativePath,
                                 std::forward<Binder>(binder));
    return stmt->ExecuteQuery();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline bool ExecuteRaw(StatementOwner& owner,
                       const std::string& queryText,
                       Binder&& binder = nullptr)
{
    auto stmt = PrepareRawStatement(owner, queryText, std::forward<Binder>(binder));
    return stmt->Execute();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline int ExecuteRawUpdate(StatementOwner& owner,
                            const std::string& queryText,
                            Binder&& binder = nullptr)
{
    auto stmt = PrepareRawStatement(owner, queryText, std::forward<Binder>(binder));
    return stmt->ExecuteUpdate();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline std::unique_ptr<IResultSet>
ExecuteRawQuery(StatementOwner& owner,
                const std::string& queryText,
                Binder&& binder = nullptr)
{
    auto stmt = PrepareRawStatement(owner, queryText, std::forward<Binder>(binder));
    return stmt->ExecuteQuery();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline bool ExecuteRawFile(StatementOwner& owner,
                           const std::string& moduleName,
                           const std::string& relativePath,
                           Binder&& binder = nullptr)
{
    auto stmt = PrepareRawFileStatement(owner, moduleName, relativePath,
                                        std::forward<Binder>(binder));
    return stmt->Execute();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline bool ExecuteRawFile(StatementOwner& owner,
                           const DatabaseConfig& config,
                           const std::string& moduleName,
                           const std::string& relativePath,
                           Binder&& binder = nullptr)
{
    auto stmt = PrepareRawFileStatement(owner, config, moduleName, relativePath,
                                        std::forward<Binder>(binder));
    return stmt->Execute();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline int ExecuteRawFileUpdate(StatementOwner& owner,
                                const std::string& moduleName,
                                const std::string& relativePath,
                                Binder&& binder = nullptr)
{
    auto stmt = PrepareRawFileStatement(owner, moduleName, relativePath,
                                        std::forward<Binder>(binder));
    return stmt->ExecuteUpdate();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline int ExecuteRawFileUpdate(StatementOwner& owner,
                                const DatabaseConfig& config,
                                const std::string& moduleName,
                                const std::string& relativePath,
                                Binder&& binder = nullptr)
{
    auto stmt = PrepareRawFileStatement(owner, config, moduleName, relativePath,
                                        std::forward<Binder>(binder));
    return stmt->ExecuteUpdate();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline std::unique_ptr<IResultSet>
ExecuteRawFileQuery(StatementOwner& owner,
                    const std::string& moduleName,
                    const std::string& relativePath,
                    Binder&& binder = nullptr)
{
    auto stmt = PrepareRawFileStatement(owner, moduleName, relativePath,
                                        std::forward<Binder>(binder));
    return stmt->ExecuteQuery();
}

template <typename StatementOwner, typename Binder = std::nullptr_t>
inline std::unique_ptr<IResultSet>
ExecuteRawFileQuery(StatementOwner& owner,
                    const DatabaseConfig& config,
                    const std::string& moduleName,
                    const std::string& relativePath,
                    Binder&& binder = nullptr)
{
    auto stmt = PrepareRawFileStatement(owner, config, moduleName, relativePath,
                                        std::forward<Binder>(binder));
    return stmt->ExecuteQuery();
}

} // namespace Network::Database::SqlScriptRunner
