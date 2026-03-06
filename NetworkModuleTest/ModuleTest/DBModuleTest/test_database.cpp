#include "DatabaseFactory.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace DocDBModule;

namespace
{
void Expect(bool condition, const std::string &message)
{
    if (!condition)
    {
        throw std::runtime_error("[ASSERT] " + message);
    }
}

bool NearlyEqual(double lhs, double rhs, double epsilon = 1e-6)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

std::string GetEnvOrDefault(const char *name, const std::string &fallback)
{
    const char *value = std::getenv(name);
    if (value != nullptr && value[0] != '\0')
    {
        return value;
    }

    return fallback;
}

bool IsTruthyEnv(const char *name)
{
    const std::string value = GetEnvOrDefault(name, "");
    return value == "1" || value == "true" || value == "TRUE" || value == "on" ||
           value == "ON";
}

void VerifyTypeRoundTrip(IStatement &statement)
{
    statement.setQuery(
        "SELECT "
        "? AS v_text, "
        "? AS v_int, "
        "? AS v_bigint, "
        "? AS v_double, "
        "? AS v_bool, "
        "? AS v_nullable");

    statement.bindParameter(1, std::string("typed-value"));
    statement.bindParameter(2, 1234);
    statement.bindParameter(3, 123456789012345LL);
    statement.bindParameter(4, 99.875);
    statement.bindParameter(5, true);
    statement.bindNullParameter(6);

    auto result = statement.executeQuery();
    Expect(result != nullptr, "ResultSet must not be null");
    Expect(result->next(), "SELECT should return one row");

    Expect(result->getString("v_text") == "typed-value", "string round-trip failed");
    Expect(result->getInt("v_int") == 1234, "int round-trip failed");
    Expect(result->getLong("v_bigint") == 123456789012345LL, "long long round-trip failed");
    Expect(NearlyEqual(result->getDouble("v_double"), 99.875, 1e-4), "double round-trip failed");
    Expect(result->getBool("v_bool"), "bool round-trip failed");
    Expect(result->isNull("v_nullable"), "NULL round-trip failed");
    Expect(!result->next(), "Unexpected extra row");
}

void TestDatabaseFactory()
{
    std::cout << "[TEST] DatabaseFactory\n";

    auto odbcDb = DatabaseFactory::createODBCDatabase();
    Expect(odbcDb != nullptr, "ODBC factory returned null");
    Expect(odbcDb->getType() == DatabaseType::ODBC, "ODBC type mismatch");

    auto oledbDb = DatabaseFactory::createOLEDBDatabase();
    Expect(oledbDb != nullptr, "OLEDB factory returned null");
    Expect(oledbDb->getType() == DatabaseType::OLEDB, "OLEDB type mismatch");

    auto genericOdbc = DatabaseFactory::createDatabase(DatabaseType::ODBC);
    auto genericOledb = DatabaseFactory::createDatabase(DatabaseType::OLEDB);
    Expect(genericOdbc != nullptr, "Generic ODBC factory returned null");
    Expect(genericOledb != nullptr, "Generic OLEDB factory returned null");
}

void TestConnectionStringUtils()
{
    std::cout << "[TEST] Connection string utilities\n";

    std::map<std::string, std::string> odbcParams = {
        {"DRIVER", "{ODBC Driver 17 for SQL Server}"},
        {"SERVER", "localhost"},
        {"DATABASE", "master"},
        {"Trusted_Connection", "Yes"}};

    std::string odbcConnStr = Utils::buildODBCConnectionString(odbcParams);
    Expect(!odbcConnStr.empty(), "ODBC connection string should not be empty");
    Expect(odbcConnStr.find("DRIVER={ODBC Driver 17 for SQL Server}") != std::string::npos,
           "ODBC driver entry missing");

    std::map<std::string, std::string> oledbParams = {
        {"Provider", "MSOLEDBSQL"},
        {"Server", "localhost"},
        {"Database", "master"},
        {"Integrated Security", "SSPI"}};

    std::string oledbConnStr = Utils::buildOLEDBConnectionString(oledbParams);
    Expect(!oledbConnStr.empty(), "OLEDB connection string should not be empty");
    Expect(oledbConnStr.find("Provider=MSOLEDBSQL") != std::string::npos,
           "OLEDB provider entry missing");
}

void TestDatabaseConfig()
{
    std::cout << "[TEST] DatabaseConfig\n";

    DatabaseConfig config;
    config.connectionString = "Server=localhost";
    config.type = DatabaseType::ODBC;
    config.connectionTimeout = 15;
    config.commandTimeout = 45;
    config.autoCommit = false;
    config.maxPoolSize = 32;
    config.minPoolSize = 4;

    Expect(config.connectionString == "Server=localhost", "connectionString mismatch");
    Expect(config.type == DatabaseType::ODBC, "type mismatch");
    Expect(config.connectionTimeout == 15, "connectionTimeout mismatch");
    Expect(config.commandTimeout == 45, "commandTimeout mismatch");
    Expect(!config.autoCommit, "autoCommit mismatch");
    Expect(config.maxPoolSize == 32, "maxPoolSize mismatch");
    Expect(config.minPoolSize == 4, "minPoolSize mismatch");
}

void TestExceptionHandling()
{
    std::cout << "[TEST] DatabaseException\n";

    try
    {
        throw DatabaseException("unit-test", 777);
    }
    catch (const DatabaseException &e)
    {
        Expect(std::string(e.what()) == "unit-test", "exception message mismatch");
        Expect(e.getErrorCode() == 777, "exception code mismatch");
    }
}

bool RunBackendTypeTest(DatabaseType type, const std::string &name,
                        const std::string &connString, bool strict)
{
    std::cout << "[TEST] Backend " << name << " type round-trip\n";

    auto database = DatabaseFactory::createDatabase(type);

    DatabaseConfig config;
    config.type = type;
    config.connectionString = connString;
    config.connectionTimeout = 15;
    config.commandTimeout = 30;
    config.autoCommit = true;

    try
    {
        database->connect(config);
    }
    catch (const DatabaseException &e)
    {
        std::cout << "  [WARN] connect failed: " << e.what() << "\n";
        if (strict)
        {
            throw;
        }
        return false;
    }

    Expect(database->isConnected(), name + " should be connected");

    auto statement = database->createStatement();
    VerifyTypeRoundTrip(*statement);

    auto connection = database->createConnection();
    connection->open(config.connectionString);
    Expect(connection->isOpen(), name + " connection should be open");
    connection->close();
    Expect(!connection->isOpen(), name + " connection should be closed");

    database->disconnect();
    Expect(!database->isConnected(), name + " should be disconnected");
    return true;
}
} // namespace

int main()
{
#if defined(_WIN32)
    try
    {
        std::cout << "=== DBModuleTest typed integration tests ===\n";

        TestDatabaseFactory();
        TestConnectionStringUtils();
        TestDatabaseConfig();
        TestExceptionHandling();

        const bool strict = IsTruthyEnv("DOCDB_REQUIRE_DB");

        const std::string odbcConn = GetEnvOrDefault(
            "DOCDB_ODBC_CONN",
            "DRIVER={ODBC Driver 17 for SQL Server};SERVER=localhost;DATABASE=master;Trusted_Connection=Yes");

        const std::string oledbConn = GetEnvOrDefault(
            "DOCDB_OLEDB_CONN",
            "Provider=MSOLEDBSQL;Server=localhost;Database=master;Integrated Security=SSPI");

        const bool odbcExecuted = RunBackendTypeTest(DatabaseType::ODBC, "ODBC", odbcConn, strict);
        const bool oledbExecuted = RunBackendTypeTest(DatabaseType::OLEDB, "OLEDB", oledbConn, strict);

        std::cout << "\n=== Summary ===\n";
        std::cout << "ODBC test executed: " << (odbcExecuted ? "YES" : "NO (skipped)") << "\n";
        std::cout << "OLEDB test executed: " << (oledbExecuted ? "YES" : "NO (skipped)") << "\n";

        if (!odbcExecuted && !oledbExecuted)
        {
            std::cout << "Set DOCDB_REQUIRE_DB=1 to fail when DB connection is unavailable.\n";
        }

        std::cout << "\nAll requested tests completed.\n";
        return 0;
    }
    catch (const DatabaseException &e)
    {
        std::cerr << "Database test failed: " << e.what() << " (Code: " << e.getErrorCode() << ")\n";
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
#else
    std::cout << "db_tests are Windows-oriented in this module. Skipping on non-Windows.\n";
    return 0;
#endif
}
