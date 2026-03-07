#include "DatabaseFactory.h"
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

DatabaseConfig BuildOledbConfig()
{
    DatabaseConfig config;
    config.type = DatabaseType::OLEDB;

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

void RunExtendedCppTypeRoundTrip(IStatement &statement)
{
    const std::int16_t valueInt16 = -30001;
    const std::uint16_t valueUInt16 = 65000;
    const std::int32_t valueInt32 = -123456789;
    const std::uint32_t valueUInt32 = 3456789012U;
    const std::int64_t valueInt64 = -7000000000123LL;
    const std::uint64_t valueUInt64 = 7000000000123ULL;
    const float valueFloat = 1.25F;
    const long double valueLongDouble = 8.125L;
    const char valueChar = 'R';
    const std::string valueDate = "2026-03-06";
    const std::string valueTime = "08:09:10.555";
    const std::vector<std::uint8_t> valueBinary = {0xBAU, 0xADU, 0xF0U, 0x0DU};
    const std::string valueBinaryHex = ToHexLiteral(valueBinary);

    statement.clearParameters();
    statement.setQuery(
        "SELECT "
        "? AS v_i16, "
        "? AS v_u16, "
        "? AS v_i32, "
        "? AS v_u32, "
        "? AS v_i64, "
        "? AS v_u64, "
        "? AS v_float, "
        "? AS v_longdouble, "
        "? AS v_char, "
        "? AS v_date, "
        "? AS v_time, "
        "? AS v_binary_hex, "
        "? AS v_nullable");

    statement.bindParameter(1, static_cast<int>(valueInt16));
    statement.bindParameter(2, static_cast<int>(valueUInt16));
    statement.bindParameter(3, static_cast<int>(valueInt32));
    statement.bindParameter(4, static_cast<long long>(valueUInt32));
    statement.bindParameter(5, static_cast<long long>(valueInt64));
    statement.bindParameter(6, static_cast<long long>(valueUInt64));
    statement.bindParameter(7, static_cast<double>(valueFloat));
    statement.bindParameter(8, static_cast<double>(valueLongDouble));
    statement.bindParameter(9, std::string(1, valueChar));
    statement.bindParameter(10, valueDate);
    statement.bindParameter(11, valueTime);
    statement.bindParameter(12, valueBinaryHex);
    statement.bindNullParameter(13);

    auto result = statement.executeQuery();
    Expect(result != nullptr, "ResultSet must not be null (extended)");
    Expect(result->next(), "SELECT should return one row (extended)");

    Expect(result->getInt("v_i16") == static_cast<int>(valueInt16), "v_i16 mismatch");
    Expect(result->getInt("v_u16") == static_cast<int>(valueUInt16), "v_u16 mismatch");
    Expect(result->getInt("v_i32") == static_cast<int>(valueInt32), "v_i32 mismatch");
    Expect(result->getLong("v_u32") == static_cast<long long>(valueUInt32), "v_u32 mismatch");
    Expect(result->getLong("v_i64") == static_cast<long long>(valueInt64), "v_i64 mismatch");
    Expect(result->getLong("v_u64") == static_cast<long long>(valueUInt64), "v_u64 mismatch");
    Expect(NearlyEqual(result->getDouble("v_float"), static_cast<double>(valueFloat), 1e-4), "v_float mismatch");
    Expect(NearlyEqual(result->getDouble("v_longdouble"), static_cast<double>(valueLongDouble), 1e-5), "v_longdouble mismatch");
    Expect(result->getString("v_char") == std::string(1, valueChar), "v_char mismatch");
    Expect(result->getString("v_date") == valueDate, "v_date mismatch");
    Expect(result->getString("v_time") == valueTime, "v_time mismatch");
    Expect(result->getString("v_binary_hex") == valueBinaryHex, "v_binary_hex mismatch");
    Expect(result->isNull("v_nullable"), "v_nullable should be NULL (extended)");
    Expect(!result->next(), "SELECT should return only one row (extended)");
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

        PrintSeparator("Case 3: extended C++ types");
        RunExtendedCppTypeRoundTrip(*statement);

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