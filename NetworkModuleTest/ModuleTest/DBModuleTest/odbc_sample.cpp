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

DatabaseConfig BuildOdbcConfig()
{
    DatabaseConfig config;
    config.type = DatabaseType::ODBC;

    // Korean: 환경변수 DOCDB_ODBC_CONN 우선. 없으면 로컬 기본값 사용.
    config.connectionString = GetEnvOrDefault(
        "DOCDB_ODBC_CONN",
        "DRIVER={ODBC Driver 17 for SQL Server};SERVER=localhost;DATABASE=master;Trusted_Connection=Yes");

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

    statement.bindParameter(1, std::string("alpha"));
    statement.bindParameter(2, 42);
    statement.bindParameter(3, 9000000000123LL);
    statement.bindParameter(4, 1234.5);
    statement.bindParameter(5, true);
    statement.bindNullParameter(6);

    auto result = statement.executeQuery();
    Expect(result != nullptr, "ResultSet must not be null");
    Expect(result->next(), "SELECT should return one row");

    Expect(result->getString("v_text") == "alpha", "v_text mismatch");
    Expect(result->getInt("v_int") == 42, "v_int mismatch");
    Expect(result->getLong("v_bigint") == 9000000000123LL, "v_bigint mismatch");
    Expect(NearlyEqual(result->getDouble("v_double"), 1234.5, 1e-3), "v_double mismatch");
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

    statement.bindParameter(1, std::string("beta"));
    statement.bindParameter(2, -7);
    statement.bindParameter(3, -123456789LL);
    statement.bindParameter(4, -98.765);
    statement.bindParameter(5, false);
    statement.bindParameter(6, std::string("not-null"));

    auto result = statement.executeQuery();
    Expect(result != nullptr, "ResultSet must not be null (2nd run)");
    Expect(result->next(), "SELECT should return one row (2nd run)");

    Expect(result->getString("v_text") == "beta", "v_text mismatch (2nd run)");
    Expect(result->getInt("v_int") == -7, "v_int mismatch (2nd run)");
    Expect(result->getLong("v_bigint") == -123456789LL, "v_bigint mismatch (2nd run)");
    Expect(NearlyEqual(result->getDouble("v_double"), -98.765, 1e-3), "v_double mismatch (2nd run)");
    Expect(!result->getBool("v_bool"), "v_bool mismatch (2nd run)");
    Expect(!result->isNull("v_nullable"), "v_nullable should not be NULL (2nd run)");
}
} // namespace

int main()
{
#if defined(_WIN32)
    try
    {
        PrintSeparator("DocDBModule ODBC Type Round-Trip Sample");

        DatabaseConfig config = BuildOdbcConfig();
        std::cout << "Connection String: " << config.connectionString << "\n";

        auto database = DatabaseFactory::createODBCDatabase();
        database->connect(config);

        Expect(database->isConnected(), "Database should be connected");

        auto statement = database->createStatement();

        PrintSeparator("Case 1: true + NULL");
        RunTypeRoundTrip(*statement);

        PrintSeparator("Case 2: false + non-NULL");
        RunTypeRoundTripFalseBool(*statement);

        database->disconnect();
        Expect(!database->isConnected(), "Database should be disconnected");

        PrintSeparator("ODBC sample completed successfully");
        return 0;
    }
    catch (const DatabaseException &e)
    {
        std::cerr << "Database Error: " << e.what() << " (Code: " << e.getErrorCode() << ")\n";
        std::cerr << "Hint: Set DOCDB_ODBC_CONN if default localhost SQL Server is unavailable.\n";
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
#else
    std::cout << "odbc_sample is Windows-only (_WIN32). Skipping.\n";
    return 0;
#endif
}
