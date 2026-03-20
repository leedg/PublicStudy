// English: TestDatabaseManager implementation
// ?쒓?: TestDatabaseManager 援ы쁽

#include "../include/TestDatabaseManager.h"
#include "../include/TestServerSqlSpec.h"
#include "../../ServerEngine/Database/DatabaseFactory.h"
#include "../../ServerEngine/Database/DatabaseModule.h"
#include "../../ServerEngine/Database/SqlModuleBootstrap.h"
#include "../../ServerEngine/Database/SqlScriptRunner.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <utility>

namespace TestServer
{

namespace
{
constexpr const char* kSqlModuleName = "TestServer";

template <typename Binder = std::nullptr_t>
std::unique_ptr<Network::Database::IStatement>
PrepareScriptStatement(Network::Database::IConnection& connection,
                       const Network::Database::DatabaseConfig& config,
                       const char* relativePath,
                       Binder&& binder = nullptr)
{
    return Network::Database::SqlScriptRunner::PrepareStatement(
        connection, config, kSqlModuleName, relativePath, std::forward<Binder>(binder));
}
} // namespace

TestServerDatabaseManager::TestServerDatabaseManager() : mIsInitialized(false)
{
}

TestServerDatabaseManager::~TestServerDatabaseManager() { ShutdownDatabase(); }

bool TestServerDatabaseManager::InitializeConnectionPool(
    const std::string& connectionString,
    int maxPoolSize,
    Network::Database::SqlDialect sqlDialectHint)
{
    if (mIsInitialized)
    {
        std::cout << "[TestServerDatabaseManager] Already initialized"
                  << std::endl;
        return true;
    }

    try
    {
        mDatabaseConfig = {};
        mDatabaseConfig.mType = Network::Database::DatabaseType::ODBC;
        mDatabaseConfig.mConnectionString = connectionString;
        const Network::Database::SqlDialect inferredSqlDialectHint =
            Network::Database::SqlScriptRunner::InferSqlDialectHint(connectionString);
        mDatabaseConfig.mSqlDialectHint =
            (sqlDialectHint != Network::Database::SqlDialect::Auto)
                ? sqlDialectHint
                : inferredSqlDialectHint;
        mDatabaseConfig.mMaxPoolSize = maxPoolSize;
        mDatabaseConfig.mMinPoolSize = 2;
        mDatabaseConfig.mConnectionTimeout = 30;

        std::string loweredConnectionString = connectionString;
        std::transform(loweredConnectionString.begin(), loweredConnectionString.end(),
                       loweredConnectionString.begin(),
                       [](unsigned char c)
                       {
                           return static_cast<char>(std::tolower(c));
                       });
        if (loweredConnectionString.find("dsn=") != std::string::npos &&
            inferredSqlDialectHint == Network::Database::SqlDialect::Auto &&
            sqlDialectHint == Network::Database::SqlDialect::Auto)
        {
            std::cout
                << "[TestServerDatabaseManager] DSN connection detected without "
                   "sqlDialectHint; generic SQL variants will be used"
                << std::endl;
        }

        auto bootstrapDatabase = Network::Database::createDatabase(mDatabaseConfig);
        if (!bootstrapDatabase)
        {
            std::cerr << "[TestServerDatabaseManager] Failed to create bootstrap database"
                      << std::endl;
            return false;
        }

        const bool bootstrapped =
            Network::Database::SqlModuleBootstrap::BootstrapModuleIfNeeded(
                *bootstrapDatabase,
                Network::TestServer::GetTestServerSqlModuleSpec());
        bootstrapDatabase->Disconnect();

        std::cout << "[TestServerDatabaseManager] "
                  << (bootstrapped ? "Initial SQL bootstrap completed"
                                   : "SQL manifest verified")
                  << std::endl;

        mDatabaseConnectionPool =
            std::make_unique<Network::Database::ConnectionPool>();

        if (!mDatabaseConnectionPool->Initialize(mDatabaseConfig))
        {
            std::cerr << "[TestServerDatabaseManager] Failed to initialize "
                         "connection pool"
                      << std::endl;
            return false;
        }

        mIsInitialized = true;
        std::cout << "[TestServerDatabaseManager] Initialized successfully"
                  << std::endl;
        std::cout << "[TestServerDatabaseManager] Pool size: " << maxPoolSize
                  << std::endl;

        return true;
    }
    catch (const Network::Database::DatabaseException& e)
    {
        std::cerr << "[TestServerDatabaseManager] Initialization error: "
                  << e.what() << std::endl;
        return false;
    }
}

bool TestServerDatabaseManager::InitializeConnectionPool(
    const std::string& connectionString,
    int maxPoolSize,
    Network::Database::DatabaseType sqlDialectHint)
{
    Network::Database::SqlDialect mappedDialect =
        Network::Database::SqlDialect::Auto;

    switch (sqlDialectHint)
    {
    case Network::Database::DatabaseType::SQLite:
        mappedDialect = Network::Database::SqlDialect::SQLite;
        break;

    case Network::Database::DatabaseType::MySQL:
        mappedDialect = Network::Database::SqlDialect::MySQL;
        break;

    case Network::Database::DatabaseType::PostgreSQL:
        mappedDialect = Network::Database::SqlDialect::PostgreSQL;
        break;

    default:
        break;
    }

    return InitializeConnectionPool(connectionString, maxPoolSize, mappedDialect);
}

void TestServerDatabaseManager::ShutdownDatabase()
{
    if (mDatabaseConnectionPool)
    {
        mDatabaseConnectionPool->Shutdown();
        mDatabaseConnectionPool.reset();
    }
    mIsInitialized = false;
    std::cout << "[TestServerDatabaseManager] Shutdown complete" << std::endl;
}

bool TestServerDatabaseManager::IsDatabaseReady() const
{
    return mIsInitialized && mDatabaseConnectionPool &&
           mDatabaseConnectionPool->IsInitialized();
}

bool TestServerDatabaseManager::SaveUserLoginEvent(uint64_t userId,
                                                   const std::string& username)
{
    if (!IsDatabaseReady())
    {
        std::cerr << "[TestServerDatabaseManager] Database not ready"
                  << std::endl;
        return false;
    }

    try
    {
        Network::Database::ScopedConnection connection(
            mDatabaseConnectionPool->GetConnection(),
            mDatabaseConnectionPool.get());
        connection->BeginTransaction();

        const auto rollback = [&connection]() {
            try
            {
                connection->RollbackTransaction();
            }
            catch (...)
            {
            }
        };

        try
        {
            auto loginStatement = PrepareScriptStatement(
                *connection,
                mDatabaseConfig,
                "SP/SP_InsertUserLoginEvent.sql",
                [&](Network::Database::IStatement& stmt) {
                    stmt.BindParameter(1, static_cast<long long>(userId));
                    stmt.BindParameter(2, username);
                });
            const int loginRowsAffected = loginStatement->ExecuteUpdate();

            auto profileStatement = PrepareScriptStatement(
                *connection,
                mDatabaseConfig,
                "SP/SP_UpsertUserProfile.sql",
                [&](Network::Database::IStatement& stmt) {
                    stmt.BindParameter(1, static_cast<long long>(userId));
                    stmt.BindParameter(2, username);
                });
            const int profileRowsAffected = profileStatement->ExecuteUpdate();

            connection->CommitTransaction();

            std::cout << "[TestServerDatabaseManager] User login saved: "
                      << username << " (login rows: " << loginRowsAffected
                      << ", profile rows: " << profileRowsAffected << ")"
                      << std::endl;

            return loginRowsAffected > 0 && profileRowsAffected > 0;
        }
        catch (...)
        {
            rollback();
            throw;
        }
    }
    catch (const Network::Database::DatabaseException& e)
    {
        std::cerr << "[TestServerDatabaseManager] SaveUserLogin error: "
                  << e.what() << std::endl;
        return false;
    }
}

bool TestServerDatabaseManager::LoadUserProfileData(uint64_t userId,
                                                    std::string& outUsername)
{
    if (!IsDatabaseReady())
    {
        std::cerr << "[TestServerDatabaseManager] Database not ready"
                  << std::endl;
        return false;
    }

    try
    {
        Network::Database::ScopedConnection connection(
            mDatabaseConnectionPool->GetConnection(),
            mDatabaseConnectionPool.get());
        auto statement = PrepareScriptStatement(
            *connection,
            mDatabaseConfig,
            "SP/SP_SelectUserProfile.sql",
            [&](Network::Database::IStatement& stmt) {
                stmt.BindParameter(1, static_cast<long long>(userId));
            });

        auto resultSet = statement->ExecuteQuery();

        bool found = false;
        if (resultSet->Next())
        {
            outUsername = resultSet->GetString("username");
            found = true;
        }

        if (found)
        {
            std::cout << "[TestServerDatabaseManager] User data loaded: "
                      << outUsername << std::endl;
        }

        return found;
    }
    catch (const Network::Database::DatabaseException& e)
    {
        std::cerr << "[TestServerDatabaseManager] LoadUserData error: "
                  << e.what() << std::endl;
        return false;
    }
}

bool TestServerDatabaseManager::PersistPlayerGameState(
    uint64_t userId, const std::string& stateData)
{
    if (!IsDatabaseReady())
    {
        std::cerr << "[TestServerDatabaseManager] Database not ready"
                  << std::endl;
        return false;
    }

    try
    {
        Network::Database::ScopedConnection connection(
            mDatabaseConnectionPool->GetConnection(),
            mDatabaseConnectionPool.get());
        auto statement = PrepareScriptStatement(
            *connection,
            mDatabaseConfig,
            "SP/SP_UpdatePlayerGameState.sql",
            [&](Network::Database::IStatement& stmt) {
                stmt.BindParameter(1, static_cast<long long>(userId));
                stmt.BindParameter(2, stateData);
            });

        const int rowsAffected = statement->ExecuteUpdate();

        std::cout << "[TestServerDatabaseManager] Game state saved for user "
                  << userId << std::endl;

        return rowsAffected > 0;
    }
    catch (const Network::Database::DatabaseException& e)
    {
        std::cerr << "[TestServerDatabaseManager] SaveGameState error: "
                  << e.what() << std::endl;
        return false;
    }
}

bool TestServerDatabaseManager::ExecuteCustomSqlQuery(const std::string& query)
{
    if (!IsDatabaseReady())
    {
        std::cerr << "[TestServerDatabaseManager] Database not ready"
                  << std::endl;
        return false;
    }

    try
    {
        Network::Database::ScopedConnection connection(
            mDatabaseConnectionPool->GetConnection(),
            mDatabaseConnectionPool.get());
        const bool result =
            Network::Database::SqlScriptRunner::ExecuteRaw(*connection, query);

        std::cout << "[TestServerDatabaseManager] Query executed: " << query
                  << std::endl;

        return result;
    }
    catch (const Network::Database::DatabaseException& e)
    {
        std::cerr << "[TestServerDatabaseManager] ExecuteQuery error: "
                  << e.what() << std::endl;
        return false;
    }
}

bool TestServerDatabaseManager::ExecuteCustomSqlFile(
    const std::string& relativePath)
{
    if (!IsDatabaseReady())
    {
        std::cerr << "[TestServerDatabaseManager] Database not ready"
                  << std::endl;
        return false;
    }

    try
    {
        Network::Database::ScopedConnection connection(
            mDatabaseConnectionPool->GetConnection(),
            mDatabaseConnectionPool.get());
        const bool result = Network::Database::SqlScriptRunner::ExecuteRawFile(
            *connection, mDatabaseConfig, kSqlModuleName, relativePath);

        std::cout << "[TestServerDatabaseManager] Query file executed: "
                  << relativePath << std::endl;

        return result;
    }
    catch (const Network::Database::DatabaseException& e)
    {
        std::cerr << "[TestServerDatabaseManager] ExecuteQueryFile error: "
                  << e.what() << std::endl;
        return false;
    }
}

} // namespace TestServer
