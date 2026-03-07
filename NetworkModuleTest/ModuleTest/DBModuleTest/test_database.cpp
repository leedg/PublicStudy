#include "DatabaseFactory.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

std::string ToHexLiteral(const std::vector<std::uint8_t> &bytes)
{
    static const char kHexDigits[] = "0123456789ABCDEF";

    std::string result = "0x";
    result.reserve(2 + (bytes.size() * 2));

    for (std::uint8_t value : bytes)
    {
        result.push_back(kHexDigits[(value >> 4U) & 0x0FU]);
        result.push_back(kHexDigits[value & 0x0FU]);
    }

    return result;
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

bool IsTruthyValue(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    return value == "1" || value == "true" || value == "on" || value == "yes";
}

bool IsTruthyEnv(const char *name)
{
    return IsTruthyValue(GetEnvOrDefault(name, ""));
}

bool GetEnvBoolOrDefault(const char *name, bool defaultValue)
{
    const char *raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0')
    {
        return defaultValue;
    }

    std::string value(raw);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (value == "1" || value == "true" || value == "on" || value == "yes")
    {
        return true;
    }

    if (value == "0" || value == "false" || value == "off" || value == "no")
    {
        return false;
    }

    return defaultValue;
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

void VerifyExtendedCppTypeRoundTrip(IStatement &statement)
{
    const std::int8_t valueInt8 = -12;
    const std::uint8_t valueUInt8 = 250;
    const std::int16_t valueInt16 = -12345;
    const std::uint16_t valueUInt16 = 54321;
    const std::int32_t valueInt32 = -2000000000;
    const std::uint32_t valueUInt32 = 4000000000U;
    const long long valueInt64 = -9000000000123LL;
    const unsigned long long valueUInt64 = 9000000000123ULL;
    const short valueShort = -2222;
    const unsigned short valueUShort = 60000;
    const long valueLong = -1200000000L;
    const unsigned long valueULong = 3000000000UL;
    const float valueFloat = 3.1415926F;
    const long double valueLongDouble = -2.718281828L;
    const bool valueBoolFalse = false;
    const char valueChar = 'Z';
    const char *valueCString = "char-pointer";
    const std::string valueString = "string-object";
    const std::string valueDate = "2026-03-06";
    const std::string valueTime = "14:15:16.123";
    const std::vector<std::uint8_t> valueBinary = {0xDEU, 0xADU, 0xBEU, 0xEFU};
    const std::string valueBinaryHex = ToHexLiteral(valueBinary);

    statement.clearParameters();
    statement.setQuery(
        "SELECT "
        "? AS v_i8, "
        "? AS v_u8, "
        "? AS v_i16, "
        "? AS v_u16, "
        "? AS v_i32, "
        "? AS v_u32, "
        "? AS v_i64, "
        "? AS v_u64, "
        "? AS v_short, "
        "? AS v_ushort, "
        "? AS v_long, "
        "? AS v_ulong, "
        "? AS v_float, "
        "? AS v_longdouble, "
        "? AS v_bool_false, "
        "? AS v_char, "
        "? AS v_cstring, "
        "? AS v_string, "
        "? AS v_date, "
        "? AS v_time, "
        "? AS v_binary_hex, "
        "? AS v_nullable");

    statement.bindParameter(1, static_cast<int>(valueInt8));
    statement.bindParameter(2, static_cast<int>(valueUInt8));
    statement.bindParameter(3, static_cast<int>(valueInt16));
    statement.bindParameter(4, static_cast<int>(valueUInt16));
    statement.bindParameter(5, static_cast<int>(valueInt32));
    statement.bindParameter(6, static_cast<long long>(valueUInt32));
    statement.bindParameter(7, static_cast<long long>(valueInt64));
    statement.bindParameter(8, static_cast<long long>(valueUInt64));
    statement.bindParameter(9, static_cast<int>(valueShort));
    statement.bindParameter(10, static_cast<int>(valueUShort));
    statement.bindParameter(11, static_cast<long long>(valueLong));
    statement.bindParameter(12, static_cast<long long>(valueULong));
    statement.bindParameter(13, static_cast<double>(valueFloat));
    statement.bindParameter(14, static_cast<double>(valueLongDouble));
    statement.bindParameter(15, valueBoolFalse);
    statement.bindParameter(16, std::string(1, valueChar));
    statement.bindParameter(17, std::string(valueCString));
    statement.bindParameter(18, valueString);
    statement.bindParameter(19, valueDate);
    statement.bindParameter(20, valueTime);
    statement.bindParameter(21, valueBinaryHex);
    statement.bindNullParameter(22);

    auto result = statement.executeQuery();
    Expect(result != nullptr, "Extended ResultSet must not be null");
    Expect(result->next(), "Extended SELECT should return one row");

    Expect(result->getInt("v_i8") == static_cast<int>(valueInt8), "int8 round-trip failed");
    Expect(result->getInt("v_u8") == static_cast<int>(valueUInt8), "uint8 round-trip failed");
    Expect(result->getInt("v_i16") == static_cast<int>(valueInt16), "int16 round-trip failed");
    Expect(result->getInt("v_u16") == static_cast<int>(valueUInt16), "uint16 round-trip failed");
    Expect(result->getInt("v_i32") == static_cast<int>(valueInt32), "int32 round-trip failed");
    Expect(result->getLong("v_u32") == static_cast<long long>(valueUInt32), "uint32 round-trip failed");
    Expect(result->getLong("v_i64") == static_cast<long long>(valueInt64), "int64 round-trip failed");
    Expect(result->getLong("v_u64") == static_cast<long long>(valueUInt64), "uint64 round-trip failed");
    Expect(result->getInt("v_short") == static_cast<int>(valueShort), "short round-trip failed");
    Expect(result->getInt("v_ushort") == static_cast<int>(valueUShort), "unsigned short round-trip failed");
    Expect(result->getLong("v_long") == static_cast<long long>(valueLong), "long round-trip failed");
    Expect(result->getLong("v_ulong") == static_cast<long long>(valueULong), "unsigned long round-trip failed");
    Expect(NearlyEqual(result->getDouble("v_float"), static_cast<double>(valueFloat), 1e-4), "float round-trip failed");
    Expect(NearlyEqual(result->getDouble("v_longdouble"), static_cast<double>(valueLongDouble), 1e-5), "long double round-trip failed");
    Expect(!result->getBool("v_bool_false"), "bool(false) round-trip failed");
    Expect(result->getString("v_char") == std::string(1, valueChar), "char round-trip failed");
    Expect(result->getString("v_cstring") == valueCString, "const char* round-trip failed");
    Expect(result->getString("v_string") == valueString, "std::string round-trip failed");
    Expect(result->getString("v_date") == valueDate, "date-string round-trip failed");
    Expect(result->getString("v_time") == valueTime, "time-string round-trip failed");
    Expect(result->getString("v_binary_hex") == valueBinaryHex, "binary-hex round-trip failed");
    Expect(result->isNull("v_nullable"), "extended NULL round-trip failed");
    Expect(!result->next(), "Unexpected extra row in extended test");
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
        {"DRIVER", "{ODBC Driver 18 for SQL Server}"},
        {"SERVER", "(localdb)\\MSSQLLocalDB"},
        {"DATABASE", "master"},
        {"Trusted_Connection", "Yes"},
        {"Encrypt", "no"}};

    std::string odbcConnStr = Utils::buildODBCConnectionString(odbcParams);
    Expect(!odbcConnStr.empty(), "ODBC connection string should not be empty");
    Expect(odbcConnStr.find("DRIVER={ODBC Driver 18 for SQL Server}") != std::string::npos,
           "ODBC driver entry missing");

    std::map<std::string, std::string> oledbParams = {
        {"Provider", "MSOLEDBSQL"},
        {"Server", "(localdb)\\MSSQLLocalDB"},
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
        Expect(database->isConnected(), name + " should be connected");

        auto statement = database->createStatement();
        std::cout << "  [CASE] basic scalar type round-trip\n";
        VerifyTypeRoundTrip(*statement);
        std::cout << "  [CASE] extended C++ type round-trip\n";
        VerifyExtendedCppTypeRoundTrip(*statement);

        auto connection = database->createConnection();
        connection->open(config.connectionString);
        Expect(connection->isOpen(), name + " connection should be open");
        connection->close();
        Expect(!connection->isOpen(), name + " connection should be closed");

        database->disconnect();
        Expect(!database->isConnected(), name + " should be disconnected");
        return true;
    }
    catch (const DatabaseException &e)
    {
        try
        {
            if (database->isConnected())
            {
                database->disconnect();
            }
        }
        catch (...)
        {
        }

        std::cout << "  [WARN] " << name << " failed: " << e.what() << "\n";
        if (strict)
        {
            throw;
        }
        return false;
    }
}
} // namespace

int main()
{
#if defined(_WIN32)
    try
    {
        std::cout << "=== DBModuleTest typed integration tests (MSSQL + MySQL) ===\n";

        TestDatabaseFactory();
        TestConnectionStringUtils();
        TestDatabaseConfig();
        TestExceptionHandling();

        const bool strict = IsTruthyEnv("DOCDB_REQUIRE_DB");

        const bool runMssql = GetEnvBoolOrDefault("DOCDB_RUN_MSSQL", true);
        const bool runMysql = GetEnvBoolOrDefault("DOCDB_RUN_MYSQL", true);
        const bool runOledb = GetEnvBoolOrDefault("DOCDB_RUN_OLEDB", runMssql);

        const std::string mssqlOdbcConn = GetEnvOrDefault(
            "DOCDB_ODBC_CONN_MSSQL",
            GetEnvOrDefault(
                "DOCDB_ODBC_CONN",
                "DRIVER={ODBC Driver 18 for SQL Server};SERVER=(localdb)\\MSSQLLocalDB;DATABASE=master;Trusted_Connection=Yes;Encrypt=no"));

        const std::string mysqlOdbcConn = GetEnvOrDefault(
            "DOCDB_ODBC_CONN_MYSQL",
            "DRIVER={MySQL ODBC 8.0 Unicode Driver};SERVER=127.0.0.1;PORT=3306;DATABASE=mysql;USER=root;PASSWORD=;OPTION=3");

        const std::string mssqlOledbConn = GetEnvOrDefault(
            "DOCDB_OLEDB_CONN_MSSQL",
            GetEnvOrDefault(
                "DOCDB_OLEDB_CONN",
                "Provider=MSOLEDBSQL;Server=(localdb)\\MSSQLLocalDB;Database=master;Integrated Security=SSPI;Encrypt=Optional"));

        bool mssqlOdbcExecuted = false;
        bool mysqlOdbcExecuted = false;
        bool mssqlOledbExecuted = false;

        if (runMssql)
        {
            mssqlOdbcExecuted = RunBackendTypeTest(DatabaseType::ODBC, "ODBC-MSSQL", mssqlOdbcConn, strict);
        }

        if (runMysql)
        {
            mysqlOdbcExecuted = RunBackendTypeTest(DatabaseType::ODBC, "ODBC-MySQL", mysqlOdbcConn, strict);
        }

        if (runOledb && runMssql)
        {
            mssqlOledbExecuted = RunBackendTypeTest(DatabaseType::OLEDB, "OLEDB-MSSQL", mssqlOledbConn, strict);
        }

        std::cout << "\n=== Summary ===\n";
        std::cout << "ODBC-MSSQL executed: " << (mssqlOdbcExecuted ? "YES" : "NO (skipped)") << "\n";
        std::cout << "ODBC-MySQL executed: " << (mysqlOdbcExecuted ? "YES" : "NO (skipped)") << "\n";
        std::cout << "OLEDB-MSSQL executed: " << (mssqlOledbExecuted ? "YES" : "NO (skipped)") << "\n";

        if (!mssqlOdbcExecuted && !mysqlOdbcExecuted && !mssqlOledbExecuted)
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