// db_functional_test.cpp
// Functional test for ServerEngine Database module — covers our bug fixes:
//   T01: Type round-trip (string/int/long long/double/bool/null)  → #1 native binding
//   T02: IsNull() then GetString() same column                    → #4 column cache
//   T03: FindColumn case-insensitive                              → #7
//   T04: Get*() before Next() returns safe default (SQLite only)  → #6 mHasData
//   T05: IConnection::BeginTransaction / Rollback                 → sanity
//   T06: IDatabase::BeginTransaction throws (ODBC only)           → #2
//
// Compile with:
//   -DUSE_SQLITE  : SQLite in-memory backend (also add -DHAVE_SQLITE3, sqlite3.c)
//   -DUSE_MSSQL   : ODBC SQL Server backend
//   -DUSE_PGSQL   : ODBC PostgreSQL backend
//   -DUSE_MYSQL   : ODBC MySQL backend
//   -DUSE_OLEDB   : OLE DB SQL Server backend (Windows only)

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

#if defined(USE_SQLITE)
#  define HAVE_SQLITE3
#  include "Database/SQLiteDatabase.h"
using DbImpl = Network::Database::SQLiteDatabase;
#elif defined(USE_MSSQL) || defined(USE_PGSQL) || defined(USE_MYSQL)
#  include "Database/ODBCDatabase.h"
using DbImpl = Network::Database::ODBCDatabase;
#elif defined(USE_OLEDB)
#  include "Database/OLEDBDatabase.h"
using DbImpl = Network::Database::OLEDBDatabase;
#else
#  error "Define USE_SQLITE, USE_MSSQL, USE_PGSQL, USE_MYSQL, or USE_OLEDB"
#endif

#include "Interfaces/DatabaseConfig.h"
#include "Interfaces/DatabaseException.h"
#include "Interfaces/IConnection.h"
#include "Interfaces/IResultSet.h"
#include "Interfaces/IStatement.h"

using namespace Network::Database;

// ── Test infrastructure ───────────────────────────────────────────────────────

static int gPass = 0, gFail = 0;

static void Check(bool cond, const char *name)
{
    if (cond) { std::cout << "  [PASS] " << name << "\n"; ++gPass; }
    else       { std::cout << "  [FAIL] " << name << "\n"; ++gFail; }
}

static void CheckThrows(std::function<void()> fn, const char *name)
{
    try
    {
        fn();
        std::cout << "  [FAIL] " << name << "  (no exception thrown)\n";
        ++gFail;
    }
    catch (...)
    {
        std::cout << "  [PASS] " << name << "\n";
        ++gPass;
    }
}

// ── Backend-specific SQL ──────────────────────────────────────────────────────

#if defined(USE_SQLITE)
static const char *kSQL_DROP   = "DROP TABLE IF EXISTS db_func_test";
static const char *kSQL_CREATE =
    "CREATE TABLE db_func_test ("
    "  c_text TEXT, c_int INTEGER, c_long INTEGER,"
    "  c_real REAL, c_bool INTEGER, c_null TEXT)";
static const char *kSQL_INSERT = "INSERT INTO db_func_test VALUES (?, ?, ?, ?, ?, ?)";
static const char *kSQL_SELECT = "SELECT * FROM db_func_test";
static const char *kSQL_COUNT  = "SELECT COUNT(*) FROM db_func_test";

#elif defined(USE_MSSQL)
static const char *kSQL_DROP   = "IF OBJECT_ID('dbo.db_func_test','U') IS NOT NULL DROP TABLE db_func_test";
static const char *kSQL_CREATE =
    "CREATE TABLE db_func_test ("
    "  c_text NVARCHAR(200), c_int INT, c_long BIGINT,"
    "  c_real FLOAT, c_bool BIT, c_null NVARCHAR(200))";
static const char *kSQL_INSERT = "INSERT INTO db_func_test VALUES (?, ?, ?, ?, ?, ?)";
static const char *kSQL_SELECT = "SELECT * FROM db_func_test";
static const char *kSQL_COUNT  = "SELECT COUNT(*) FROM db_func_test";

#elif defined(USE_PGSQL)
static const char *kSQL_DROP   = "DROP TABLE IF EXISTS db_func_test";
static const char *kSQL_CREATE =
    "CREATE TABLE db_func_test ("
    "  c_text TEXT, c_int INTEGER, c_long BIGINT,"
    "  c_real DOUBLE PRECISION, c_bool BOOLEAN, c_null TEXT)";
static const char *kSQL_INSERT = "INSERT INTO db_func_test VALUES (?, ?, ?, ?, ?, ?)";
static const char *kSQL_SELECT = "SELECT * FROM db_func_test";
static const char *kSQL_COUNT  = "SELECT COUNT(*) FROM db_func_test";

#elif defined(USE_MYSQL)
static const char *kSQL_DROP   = "DROP TABLE IF EXISTS db_func_test";
static const char *kSQL_CREATE =
    "CREATE TABLE db_func_test ("
    "  c_text TEXT, c_int INT, c_long BIGINT,"
    "  c_real DOUBLE, c_bool TINYINT(1), c_null TEXT)";
static const char *kSQL_INSERT = "INSERT INTO db_func_test VALUES (?, ?, ?, ?, ?, ?)";
static const char *kSQL_SELECT = "SELECT * FROM db_func_test";
static const char *kSQL_COUNT  = "SELECT COUNT(*) FROM db_func_test";

#elif defined(USE_OLEDB)
// English: OLE DB uses SQLOLEDB provider against SQL Server — same DDL as MSSQL
// 한글: OLE DB는 SQLOLEDB 공급자로 SQL Server에 접속 — MSSQL과 동일한 DDL
static const char *kSQL_DROP   = "IF OBJECT_ID('dbo.db_func_test','U') IS NOT NULL DROP TABLE db_func_test";
static const char *kSQL_CREATE =
    "CREATE TABLE db_func_test ("
    "  c_text NVARCHAR(200), c_int INT, c_long BIGINT,"
    "  c_real FLOAT, c_bool BIT, c_null NVARCHAR(200))";
static const char *kSQL_INSERT = "INSERT INTO db_func_test VALUES (?, ?, ?, ?, ?, ?)";
static const char *kSQL_SELECT = "SELECT * FROM db_func_test";
static const char *kSQL_COUNT  = "SELECT COUNT(*) FROM db_func_test";
#endif

// ── Helpers ───────────────────────────────────────────────────────────────────

static void Exec(IDatabase &db, const char *sql)
{
    auto s = db.CreateStatement();
    s->SetQuery(sql);
    s->Execute();
}

static void ExecConn(IConnection &conn, const char *sql)
{
    auto s = conn.CreateStatement();
    s->SetQuery(sql);
    s->Execute();
}

static int CountRows(IDatabase &db)
{
    auto s  = db.CreateStatement();
    s->SetQuery(kSQL_COUNT);
    auto rs = s->ExecuteQuery();
    return rs->Next() ? rs->GetInt(0) : 0;
}

static int CountRowsConn(IConnection &conn)
{
    auto s  = conn.CreateStatement();
    s->SetQuery(kSQL_COUNT);
    auto rs = s->ExecuteQuery();
    return rs->Next() ? rs->GetInt(0) : 0;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void T01_TypeRoundTrip(IDatabase &db)
{
    std::cout << "\n[T01] Type round-trip via INSERT + SELECT\n";

    Exec(db, kSQL_DROP);
    Exec(db, kSQL_CREATE);

    {
        auto s = db.CreateStatement();
        s->SetQuery(kSQL_INSERT);
        s->BindParameter(1, std::string("hello-world"));
        s->BindParameter(2, 42);
        s->BindParameter(3, 9876543210123LL);
        s->BindParameter(4, 3.141592653589793);
        s->BindParameter(5, true);
        s->BindNullParameter(6);
        s->ExecuteUpdate();
    }

    {
        auto s  = db.CreateStatement();
        s->SetQuery(kSQL_SELECT);
        auto rs = s->ExecuteQuery();

        Check(rs->Next(),                                              "T01-0: Next() ok");
        Check(rs->GetString(0) == "hello-world",                      "T01-1: string");
        Check(rs->GetInt(1)    == 42,                                 "T01-2: int");
        Check(rs->GetLong(2)   == 9876543210123LL,                    "T01-3: long long");
        Check(std::fabs(rs->GetDouble(3) - 3.141592653589793) < 1e-9, "T01-4: double precision");
        Check(rs->GetBool(4)   == true,                               "T01-5: bool");
        Check(rs->IsNull(5),                                          "T01-6: null");
        Check(!rs->Next(),                                            "T01-7: no extra rows");
    }
}

static void T02_IsNullThenGetString(IDatabase &db)
{
    std::cout << "\n[T02] IsNull() then GetString() same column (column cache)\n";

    auto s  = db.CreateStatement();
    s->SetQuery(kSQL_SELECT);
    auto rs = s->ExecuteQuery();

    Check(rs->Next(), "T02-0: Next() ok");

    // Key: call IsNull first, then GetString on the SAME column
    // Bug (pre-fix): IsNull consumed SQLGetData cursor → GetString returned ""
    bool isNull = rs->IsNull(0);
    std::string val = rs->GetString(0);

    Check(!isNull,               "T02-1: IsNull(c_text) == false");
    Check(val == "hello-world",  "T02-2: GetString after IsNull returns correct value");

    // Also verify by column name
    bool isNullByName  = rs->IsNull("c_text");
    std::string valByName = rs->GetString("c_text");
    Check(!isNullByName,         "T02-3: IsNull('c_text') == false");
    Check(valByName == "hello-world", "T02-4: GetString by name after IsNull ok");
}

static void T03_CaseInsensitiveColumn(IDatabase &db)
{
    std::cout << "\n[T03] FindColumn case-insensitive\n";

    auto s  = db.CreateStatement();
    s->SetQuery(kSQL_SELECT);
    auto rs = s->ExecuteQuery();
    rs->Next();

    // Drivers vary: SQL Server returns uppercase, PostgreSQL lowercase, SQLite as-declared
    // After fix: all three name forms should resolve to the same column
    try
    {
        std::string lo = rs->GetString("c_text");
        std::string up = rs->GetString("C_TEXT");
        std::string mi = rs->GetString("C_Text");
        Check(lo == "hello-world", "T03-1: GetString('c_text') correct");
        Check(up == lo,            "T03-2: GetString('C_TEXT') == lowercase result");
        Check(mi == lo,            "T03-3: GetString('C_Text') == lowercase result");
    }
    catch (const DatabaseException &e)
    {
        std::cout << "  [FAIL] T03: column lookup threw: " << e.what() << "\n";
        ++gFail;
    }
}

#if defined(USE_SQLITE)
static void T04_GetBeforeNext(IDatabase &db)
{
    std::cout << "\n[T04] SQLite: Get*() before Next() returns safe default (not UB)\n";

    auto s  = db.CreateStatement();
    s->SetQuery(kSQL_SELECT);
    auto rs = s->ExecuteQuery();
    // Intentionally do NOT call Next()

    // Pre-fix: sqlite3_column_* with no prior SQLITE_ROW step = undefined behaviour
    // Post-fix: mHasData guard returns safe defaults
    std::string strVal = rs->GetString(0);
    int         intVal = rs->GetInt(1);
    long long   lonVal = rs->GetLong(2);
    double      dblVal = rs->GetDouble(3);

    Check(strVal.empty(), "T04-1: GetString before Next() returns empty");
    Check(intVal == 0,    "T04-2: GetInt before Next() returns 0");
    Check(lonVal == 0LL,  "T04-3: GetLong before Next() returns 0");
    Check(dblVal == 0.0,  "T04-4: GetDouble before Next() returns 0.0");
}
#endif

static void T05_ConnectionTransaction(IDatabase &db, const char *connStr)
{
    std::cout << "\n[T05] IConnection::BeginTransaction / Rollback\n";

    // Use a dedicated connection for isolation
    auto conn = db.CreateConnection();
    conn->Open(connStr);

    int baseBefore = CountRowsConn(*conn);
    Check(baseBefore >= 1, "T05-0: table has at least one row (from T01)");

    // Insert a row inside a transaction, then rollback
    conn->BeginTransaction();
    {
        auto s = conn->CreateStatement();
        s->SetQuery(kSQL_INSERT);
        s->BindParameter(1, std::string("rollback-row"));
        s->BindParameter(2, 999);
        s->BindParameter(3, 0LL);
        s->BindParameter(4, 0.0);
        s->BindParameter(5, false);
        s->BindNullParameter(6);
        s->ExecuteUpdate();
    }
    Check(CountRowsConn(*conn) == baseBefore + 1, "T05-1: row visible before rollback");

    conn->RollbackTransaction();
    Check(CountRowsConn(*conn) == baseBefore,     "T05-2: row absent after rollback");
}

#if defined(USE_MSSQL) || defined(USE_PGSQL) || defined(USE_MYSQL) || defined(USE_OLEDB)
static void T06_DatabaseTransactionThrows(IDatabase &db)
{
    std::cout << "\n[T06] IDatabase::BeginTransaction throws (per design)\n";

    CheckThrows([&db]() { db.BeginTransaction(); },   "T06-1: BeginTransaction throws");
    CheckThrows([&db]() { db.CommitTransaction(); },  "T06-2: CommitTransaction throws");
    CheckThrows([&db]() { db.RollbackTransaction(); },"T06-3: RollbackTransaction throws");
}
#endif

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[])
{
#if defined(USE_SQLITE)
    const char *connStr = ":memory:";
    (void)argc; (void)argv;
#else
    // ODBC: connection string from first CLI argument
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " \"<ODBC connection string>\"\n";
        return 2;
    }
    const char *connStr = argv[1];
#endif

    std::cout << "=== DB Functional Test [";
#if defined(USE_SQLITE)
    std::cout << "SQLite in-memory";
#elif defined(USE_MSSQL)
    std::cout << "ODBC / SQL Server";
#elif defined(USE_PGSQL)
    std::cout << "ODBC / PostgreSQL";
#elif defined(USE_MYSQL)
    std::cout << "ODBC / MySQL";
#elif defined(USE_OLEDB)
    std::cout << "OLE DB / SQL Server";
#endif
    std::cout << "] ===\n";

    DbImpl db;
    try
    {
        DatabaseConfig cfg;
        cfg.mConnectionString = connStr;
        db.Connect(cfg);
        std::cout << "Connected.\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "[FATAL] Connect failed: " << e.what() << "\n";
        return 2;
    }

    auto RunTest = [](const char *name, std::function<void()> fn) {
        try { fn(); }
        catch (const std::exception &e)
        {
            std::cout << "  [ERROR] " << name << ": " << e.what() << "\n";
            ++gFail;
        }
    };

    RunTest("T01", [&]() { T01_TypeRoundTrip(db); });
    RunTest("T02", [&]() { T02_IsNullThenGetString(db); });
    RunTest("T03", [&]() { T03_CaseInsensitiveColumn(db); });
#if defined(USE_SQLITE)
    RunTest("T04", [&]() { T04_GetBeforeNext(db); });
#endif
    RunTest("T05", [&]() { T05_ConnectionTransaction(db, connStr); });
#if defined(USE_MSSQL) || defined(USE_PGSQL) || defined(USE_MYSQL) || defined(USE_OLEDB)
    RunTest("T06", [&]() { T06_DatabaseTransactionThrows(db); });
#endif

    // Cleanup
    try { Exec(db, kSQL_DROP); } catch (...) {}

    db.Disconnect();

    std::cout << "\n=== Result: "
              << gPass << " passed, " << gFail << " failed"
              << (gFail > 0 ? " *** FAILED ***" : " (all OK)")
              << " ===\n";

    return gFail > 0 ? 1 : 0;
}
