#include "../../../Server/TestServer/include/TestDatabaseManager.h"
#include "../../../Server/ServerEngine/Database/DatabaseModule.h"
#include "../../../Server/ServerEngine/Database/SqlScriptRunner.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
Network::Database::SqlDialect ParseDialectHint(const std::string& value)
{
    if (value == "mysql")
    {
        return Network::Database::SqlDialect::MySQL;
    }

    if (value == "sqlite")
    {
        return Network::Database::SqlDialect::SQLite;
    }

    if (value == "postgres" || value == "postgresql")
    {
        return Network::Database::SqlDialect::PostgreSQL;
    }

    if (value == "sqlserver" || value == "mssql")
    {
        return Network::Database::SqlDialect::SQLServer;
    }

    if (value == "odbc" || value == "generic")
    {
        return Network::Database::SqlDialect::Generic;
    }

    throw std::runtime_error("Unsupported dialect hint: " + value);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: testserver_bootstrap_smoke <connection-string> [dialect]"
                  << std::endl;
        return 2;
    }

    const std::string connectionString = argv[1];
    const Network::Database::SqlDialect sqlDialectHint =
        (argc >= 3)
            ? ParseDialectHint(argv[2])
            : Network::Database::SqlDialect::Auto;

    TestServer::TestServerDatabaseManager manager;
    if (!manager.InitializeConnectionPool(connectionString, 2, sqlDialectHint))
    {
        std::cerr << "InitializeConnectionPool failed on first bootstrap" << std::endl;
        return 3;
    }

    if (!manager.SaveUserLoginEvent(1001, "codex_mysql"))
    {
        std::cerr << "SaveUserLoginEvent failed" << std::endl;
        return 4;
    }

    std::string username;
    if (!manager.LoadUserProfileData(1001, username))
    {
        std::cerr << "LoadUserProfileData failed" << std::endl;
        return 5;
    }

    if (username != "codex_mysql")
    {
        std::cerr << "Unexpected username: " << username << std::endl;
        return 6;
    }

    if (!manager.ExecuteCustomSqlQuery(
            "UPDATE T_Users SET username = 'codex_raw_sql' WHERE user_id = 1001"))
    {
        std::cerr << "ExecuteCustomSqlQuery failed" << std::endl;
        return 7;
    }

    if (!manager.LoadUserProfileData(1001, username))
    {
        std::cerr << "LoadUserProfileData failed after raw SQL" << std::endl;
        return 8;
    }

    if (username != "codex_raw_sql")
    {
        std::cerr << "Unexpected raw SQL username: " << username << std::endl;
        return 9;
    }

    Network::Database::DatabaseConfig rawConfig;
    rawConfig.mType = Network::Database::DatabaseType::ODBC;
    rawConfig.mConnectionString = connectionString;
    rawConfig.mSqlDialectHint =
        (sqlDialectHint != Network::Database::SqlDialect::Auto &&
         sqlDialectHint != Network::Database::SqlDialect::Generic)
            ? sqlDialectHint
            : Network::Database::SqlScriptRunner::InferSqlDialectHint(connectionString);

    auto rawDatabase = Network::Database::createDatabase(rawConfig);
    if (!rawDatabase)
    {
        std::cerr << "Failed to create raw SQL database" << std::endl;
        return 10;
    }

    const int rawRowsAffected =
        Network::Database::SqlScriptRunner::ExecuteRawFileUpdate(
            *rawDatabase,
            rawConfig,
            "TestServer",
            "RAW/RQ_UpdateUserProfileName.sql",
            [](Network::Database::IStatement& stmt)
            {
                stmt.BindParameter(1, std::string("codex_bound_raw"));
                stmt.BindParameter(2, 1001LL);
            });
    if (rawRowsAffected != 1)
    {
        std::cerr << "Unexpected raw update row count: " << rawRowsAffected
                  << std::endl;
        return 11;
    }

    if (!manager.LoadUserProfileData(1001, username))
    {
        std::cerr << "LoadUserProfileData failed after bound raw SQL" << std::endl;
        return 12;
    }

    if (username != "codex_bound_raw")
    {
        std::cerr << "Unexpected bound raw SQL username: " << username << std::endl;
        return 13;
    }

    if (!manager.PersistPlayerGameState(1001, "{\"stage\":1}"))
    {
        std::cerr << "PersistPlayerGameState failed" << std::endl;
        return 14;
    }

    manager.ShutdownDatabase();

    TestServer::TestServerDatabaseManager secondManager;
    if (!secondManager.InitializeConnectionPool(connectionString, 2, sqlDialectHint))
    {
        std::cerr << "InitializeConnectionPool failed on second attach" << std::endl;
        return 15;
    }

    secondManager.ShutdownDatabase();

    std::cout << "TestServer bootstrap smoke passed" << std::endl;
    return 0;
}
