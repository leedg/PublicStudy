#include "DatabaseFactory.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace DocDBModule;

namespace
{
void PrintSeparator(const std::string &title)
{
    std::cout << "\n" << std::string(72, '=') << "\n";
    std::cout << title << "\n";
    std::cout << std::string(72, '=') << "\n";
}

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

DatabaseConfig BuildOledbConfig()
{
    DatabaseConfig config;
    config.type = DatabaseType::OLEDB;

    // Korean: 환경변수 DOCDB_OLEDB_CONN 우선. 없으면 로컬 기본값 사용.
    config.connectionString = GetEnvOrDefault(
        "DOCDB_OLEDB_CONN",
        "Provider=MSOLEDBSQL;Server=localhost;Database=master;Integrated Security=SSPI");

    config.connectionTimeout = 30;
    config.commandTimeout = 30;
    config.autoCommit = true;
    return config;
}

void RunTypeRoundTrip(IStatement &statement)
{
    statement.setQuery(
        "SELECT "
        "? AS v_text, "
        "? AS v_int, "
        "? AS v_bigint, "
        "? AS v_double, "
        "? AS v_bool, "
        "? AS v_nullable");

    statement.bindParameter(1, std::string("gamma"));
    statement.bindParameter(2, 77);
    statement.bindParameter(3, 1234567890123LL);
    statement.bindParameter(4, 0.125);
    statement.bindParameter(5, true);
    statement.bindNullParameter(6);

    auto result = statement.executeQuery();
    Expect(result != nullptr, "ResultSet must not be null");
    Expect(result->next(), "SELECT should return one row");

    Expect(result->getString("v_text") == "gamma", "v_text mismatch");
    Expect(result->getInt("v_int") == 77, "v_int mismatch");
    Expect(result->getLong("v_bigint") == 1234567890123LL, "v_bigint mismatch");
    Expect(NearlyEqual(result->getDouble("v_double"), 0.125, 1e-6), "v_double mismatch");
    Expect(result->getBool("v_bool"), "v_bool mismatch");
    Expect(result->isNull("v_nullable"), "v_nullable should be NULL");

    Expect(!result->next(), "SELECT should return only one row");
}

void RunTypeRoundTripFalseBool(IStatement &statement)
{
    statement.clearParameters();
    statement.setQuery(
        "SELECT "
        "? AS v_text, "
        "? AS v_int, "
        "? AS v_bigint, "
        "? AS v_double, "
        "? AS v_bool, "
        "? AS v_nullable");

    statement.bindParameter(1, std::string("delta"));
    statement.bindParameter(2, -11);
    statement.bindParameter(3, -444444444444LL);
    statement.bindParameter(4, -3.5);
    statement.bindParameter(5, false);
    statement.bindParameter(6, std::string("not-null"));

    auto result = statement.executeQuery();
    Expect(result != nullptr, "ResultSet must not be null (2nd run)");
    Expect(result->next(), "SELECT should return one row (2nd run)");

    Expect(result->getString("v_text") == "delta", "v_text mismatch (2nd run)");
    Expect(result->getInt("v_int") == -11, "v_int mismatch (2nd run)");
    Expect(result->getLong("v_bigint") == -444444444444LL, "v_bigint mismatch (2nd run)");
    Expect(NearlyEqual(result->getDouble("v_double"), -3.5, 1e-6), "v_double mismatch (2nd run)");
    Expect(!result->getBool("v_bool"), "v_bool mismatch (2nd run)");
    Expect(!result->isNull("v_nullable"), "v_nullable should not be NULL (2nd run)");
}
} // namespace

int main()
{
#if defined(_WIN32)
    try
    {
        PrintSeparator("DocDBModule OLEDB Type Round-Trip Sample");

        DatabaseConfig config = BuildOledbConfig();
        std::cout << "Connection String: " << config.connectionString << "\n";

        auto database = DatabaseFactory::createOLEDBDatabase();
        database->connect(config);

        Expect(database->isConnected(), "Database should be connected");

        auto statement = database->createStatement();

        PrintSeparator("Case 1: true + NULL");
        RunTypeRoundTrip(*statement);

        PrintSeparator("Case 2: false + non-NULL");
        RunTypeRoundTripFalseBool(*statement);

        database->disconnect();
        Expect(!database->isConnected(), "Database should be disconnected");

        PrintSeparator("OLEDB sample completed successfully");
        return 0;
    }
    catch (const DatabaseException &e)
    {
        std::cerr << "Database Error: " << e.what() << " (Code: " << e.getErrorCode() << ")\n";
        std::cerr << "Hint: Set DOCDB_OLEDB_CONN if custom provider settings are required.\n";
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
#else
    std::cout << "oledb_sample is Windows-only (_WIN32). Skipping.\n";
    return 0;
#endif
}
