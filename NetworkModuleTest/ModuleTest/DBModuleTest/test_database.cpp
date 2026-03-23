// test_database.cpp
// Network::Database 모듈 수동 테스트 (DBModuleTest)
//
// Visual Studio 에서 바로 빌드 후 실행 가능.
// 백엔드를 메뉴로 선택하고, 연결 문자열을 입력하거나 기본값을 사용한다.
//
// 커버하는 테스트:
//   T01: 타입 라운드트립 (string/int/long long/double/bool/null)
//   T02: IsNull() 후 GetString() 같은 컬럼 (컬럼 캐시)
//   T03: FindColumn 대소문자 무관
//   T04: Get*() before Next() — 안전한 기본값 (SQLite only)
//   T05: IConnection::BeginTransaction / Rollback
//   T06: IDatabase::BeginTransaction throws (ODBC / OLE DB)

#include "Database/DatabaseFactory.h"
#include "Interfaces/DatabaseConfig.h"
#include "Interfaces/DatabaseException.h"
#include "Interfaces/IConnection.h"
#include "Interfaces/IDatabase.h"
#include "Interfaces/IResultSet.h"
#include "Interfaces/IStatement.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace Network::Database;

// ── 색상 출력 (Windows ANSI) ───────────────────────────────────────────────

#ifdef _WIN32
#  include <windows.h>
static void EnableAnsi()
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode))
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#else
static void EnableAnsi() {}
#endif

#define CLR_RESET  "\033[0m"
#define CLR_GREEN  "\033[32m"
#define CLR_RED    "\033[31m"
#define CLR_YELLOW "\033[33m"
#define CLR_CYAN   "\033[36m"
#define CLR_MAG    "\033[35m"

// ── 테스트 카운터 ──────────────────────────────────────────────────────────

static int gPass = 0;
static int gFail = 0;

static void ResetCounters() { gPass = gFail = 0; }

static void Check(bool cond, const char *name)
{
    if (cond)
    {
        std::cout << CLR_GREEN "  [PASS] " CLR_RESET << name << "\n";
        ++gPass;
    }
    else
    {
        std::cout << CLR_RED   "  [FAIL] " CLR_RESET << name << "\n";
        ++gFail;
    }
}

static void CheckThrows(std::function<void()> fn, const char *name)
{
    try { fn(); std::cout << CLR_RED "  [FAIL] " CLR_RESET << name << "  (no exception thrown)\n"; ++gFail; }
    catch (...) { std::cout << CLR_GREEN "  [PASS] " CLR_RESET << name << "\n"; ++gPass; }
}

// ── 백엔드별 SQL ───────────────────────────────────────────────────────────

enum class Backend { SQLite, MSSQL_ODBC, PgSQL_ODBC, MySQL_ODBC, OleDB };

struct BackendSQL
{
    const char *drop;
    const char *create;
    const char *insert;
    const char *select;
    const char *count;
};

static BackendSQL GetSQL(Backend b)
{
    switch (b)
    {
    case Backend::SQLite:
        return {
            "DROP TABLE IF EXISTS db_func_test",
            "CREATE TABLE db_func_test ("
            "  c_text TEXT, c_int INTEGER, c_long INTEGER,"
            "  c_real REAL, c_bool INTEGER, c_null TEXT)",
            "INSERT INTO db_func_test VALUES (?, ?, ?, ?, ?, ?)",
            "SELECT * FROM db_func_test",
            "SELECT COUNT(*) FROM db_func_test"
        };
    case Backend::PgSQL_ODBC:
        return {
            "DROP TABLE IF EXISTS db_func_test",
            "CREATE TABLE db_func_test ("
            "  c_text TEXT, c_int INTEGER, c_long BIGINT,"
            "  c_real DOUBLE PRECISION, c_bool BOOLEAN, c_null TEXT)",
            "INSERT INTO db_func_test VALUES (?, ?, ?, ?, ?, ?)",
            "SELECT * FROM db_func_test",
            "SELECT COUNT(*) FROM db_func_test"
        };
    case Backend::MySQL_ODBC:
        return {
            "DROP TABLE IF EXISTS db_func_test",
            "CREATE TABLE db_func_test ("
            "  c_text TEXT, c_int INT, c_long BIGINT,"
            "  c_real DOUBLE, c_bool TINYINT(1), c_null TEXT)",
            "INSERT INTO db_func_test VALUES (?, ?, ?, ?, ?, ?)",
            "SELECT * FROM db_func_test",
            "SELECT COUNT(*) FROM db_func_test"
        };
    default: // MSSQL_ODBC, OleDB
        return {
            "IF OBJECT_ID('dbo.db_func_test','U') IS NOT NULL DROP TABLE db_func_test",
            "CREATE TABLE db_func_test ("
            "  c_text NVARCHAR(200), c_int INT, c_long BIGINT,"
            "  c_real FLOAT, c_bool BIT, c_null NVARCHAR(200))",
            "INSERT INTO db_func_test VALUES (?, ?, ?, ?, ?, ?)",
            "SELECT * FROM db_func_test",
            "SELECT COUNT(*) FROM db_func_test"
        };
    }
}

// ── 기본 연결 문자열 (환경변수 우선) ──────────────────────────────────────

static std::string GetEnv(const char *name, const std::string &fallback)
{
    const char *v = std::getenv(name);
    return (v && v[0]) ? v : fallback;
}

static std::string DefaultConnStr(Backend b)
{
    switch (b)
    {
    case Backend::SQLite:
        return ":memory:";
    case Backend::MSSQL_ODBC:
        return GetEnv("DB_MSSQL_ODBC",
            "Driver={ODBC Driver 17 for SQL Server};"
            "Server=localhost,1433;Database=db_func_test;"
            "UID=sa;PWD=Test1234!;TrustServerCertificate=yes;");
    case Backend::PgSQL_ODBC:
        return GetEnv("DB_PGSQL_ODBC",
            "Driver={PostgreSQL Unicode};"
            "Server=localhost;Port=5432;Database=db_func_test;"
            "UID=postgres;PWD=Test1234!;");
    case Backend::MySQL_ODBC:
        return GetEnv("DB_MYSQL_ODBC",
            "Driver={MySQL ODBC 8.4 Unicode Driver};"
            "Server=127.0.0.1;Port=3306;Database=db_func_test;"
            "UID=root;PWD=Test1234!;");
    case Backend::OleDB:
        return GetEnv("DB_OLEDB",
            "Provider=SQLOLEDB;"
            "Data Source=localhost,1433;Initial Catalog=db_func_test;"
            "User Id=sa;Password=Test1234!;TrustServerCertificate=yes;");
    default: return "";
    }
}

// ── 헬퍼 ──────────────────────────────────────────────────────────────────

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

static int CountRows(IDatabase &db, const char *countSql)
{
    auto s  = db.CreateStatement();
    s->SetQuery(countSql);
    auto rs = s->ExecuteQuery();
    return rs->Next() ? rs->GetInt(0) : 0;
}

static int CountRowsConn(IConnection &conn, const char *countSql)
{
    auto s  = conn.CreateStatement();
    s->SetQuery(countSql);
    auto rs = s->ExecuteQuery();
    return rs->Next() ? rs->GetInt(0) : 0;
}

// ── T01: 타입 라운드트립 ──────────────────────────────────────────────────

static void T01_TypeRoundTrip(IDatabase &db, const BackendSQL &sql)
{
    std::cout << "\n" CLR_CYAN "[T01] 타입 라운드트립 (string/int/long long/double/bool/null)" CLR_RESET "\n";

    Exec(db, sql.drop);
    Exec(db, sql.create);

    {
        auto s = db.CreateStatement();
        s->SetQuery(sql.insert);
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
        s->SetQuery(sql.select);
        auto rs = s->ExecuteQuery();

        Check(rs->Next(),                                              "T01-0: Next() ok");
        Check(rs->GetString(0) == "hello-world",                      "T01-1: string");
        Check(rs->GetInt(1)    == 42,                                 "T01-2: int");
        Check(rs->GetLong(2)   == 9876543210123LL,                    "T01-3: long long");
        Check(std::fabs(rs->GetDouble(3) - 3.141592653589793) < 1e-9,"T01-4: double precision");
        Check(rs->GetBool(4)   == true,                               "T01-5: bool");
        Check(rs->IsNull(5),                                          "T01-6: null");
        Check(!rs->Next(),                                            "T01-7: no extra rows");
    }
}

// ── T02: IsNull() 후 GetString() 동일 컬럼 ───────────────────────────────

static void T02_IsNullThenGetString(IDatabase &db, const BackendSQL &sql)
{
    std::cout << "\n" CLR_CYAN "[T02] IsNull() 후 GetString() 동일 컬럼 (컬럼 캐시)" CLR_RESET "\n";

    auto s  = db.CreateStatement();
    s->SetQuery(sql.select);
    auto rs = s->ExecuteQuery();

    Check(rs->Next(), "T02-0: Next() ok");

    bool isNull  = rs->IsNull(0);
    std::string val = rs->GetString(0);

    Check(!isNull,              "T02-1: IsNull(c_text) == false");
    Check(val == "hello-world", "T02-2: GetString after IsNull returns correct value");

    bool isNullByName     = rs->IsNull("c_text");
    std::string valByName = rs->GetString("c_text");
    Check(!isNullByName,             "T02-3: IsNull('c_text') == false");
    Check(valByName == "hello-world","T02-4: GetString by name after IsNull ok");
}

// ── T03: FindColumn 대소문자 무관 ─────────────────────────────────────────

static void T03_CaseInsensitiveColumn(IDatabase &db, const BackendSQL &sql)
{
    std::cout << "\n" CLR_CYAN "[T03] FindColumn 대소문자 무관" CLR_RESET "\n";

    auto s  = db.CreateStatement();
    s->SetQuery(sql.select);
    auto rs = s->ExecuteQuery();
    rs->Next();

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
        std::cout << CLR_RED "  [FAIL] " CLR_RESET "T03: column lookup threw: " << e.what() << "\n";
        ++gFail;
    }
}

// ── T04: SQLite only — Get*() before Next() ───────────────────────────────

static void T04_GetBeforeNext_SQLite(IDatabase &db, const BackendSQL &sql)
{
    std::cout << "\n" CLR_CYAN "[T04] SQLite: Get*() before Next() — 안전한 기본값" CLR_RESET "\n";

    auto s  = db.CreateStatement();
    s->SetQuery(sql.select);
    auto rs = s->ExecuteQuery();
    // 의도적으로 Next() 호출 안 함

    std::string strVal = rs->GetString(0);
    int         intVal = rs->GetInt(1);
    long long   lonVal = rs->GetLong(2);
    double      dblVal = rs->GetDouble(3);

    Check(strVal.empty(), "T04-1: GetString before Next() returns empty");
    Check(intVal == 0,    "T04-2: GetInt before Next() returns 0");
    Check(lonVal == 0LL,  "T04-3: GetLong before Next() returns 0");
    Check(dblVal == 0.0,  "T04-4: GetDouble before Next() returns 0.0");
}

// ── T05: IConnection BeginTransaction / Rollback ──────────────────────────

static void T05_ConnectionTransaction(IDatabase &db, const BackendSQL &sql,
                                      const std::string &connStr)
{
    std::cout << "\n" CLR_CYAN "[T05] IConnection::BeginTransaction / Rollback" CLR_RESET "\n";

    auto conn = db.CreateConnection();
    conn->Open(connStr);

    int baseBefore = CountRowsConn(*conn, sql.count);
    Check(baseBefore >= 1, "T05-0: table has at least one row (from T01)");

    conn->BeginTransaction();
    {
        auto s = conn->CreateStatement();
        s->SetQuery(sql.insert);
        s->BindParameter(1, std::string("rollback-row"));
        s->BindParameter(2, 999);
        s->BindParameter(3, 0LL);
        s->BindParameter(4, 0.0);
        s->BindParameter(5, false);
        s->BindNullParameter(6);
        s->ExecuteUpdate();
    }
    Check(CountRowsConn(*conn, sql.count) == baseBefore + 1, "T05-1: row visible before rollback");

    conn->RollbackTransaction();
    Check(CountRowsConn(*conn, sql.count) == baseBefore,     "T05-2: row absent after rollback");
}

// ── T06: IDatabase::BeginTransaction throws ───────────────────────────────

static void T06_DatabaseTransactionThrows(IDatabase &db)
{
    std::cout << "\n" CLR_CYAN "[T06] IDatabase::BeginTransaction throws (per design)" CLR_RESET "\n";

    CheckThrows([&db]() { db.BeginTransaction();    }, "T06-1: BeginTransaction throws");
    CheckThrows([&db]() { db.CommitTransaction();   }, "T06-2: CommitTransaction throws");
    CheckThrows([&db]() { db.RollbackTransaction(); }, "T06-3: RollbackTransaction throws");
}

// ── 전체 테스트 수행 ──────────────────────────────────────────────────────

static bool RunAllTests(Backend backend, const std::string &connStr)
{
    BackendSQL sql = GetSQL(backend);
    bool isSQLite  = (backend == Backend::SQLite);

    // DB 생성
    std::unique_ptr<IDatabase> db;
    switch (backend)
    {
    case Backend::SQLite:
        db = DatabaseFactory::CreateSQLiteDatabase();
        break;
    case Backend::OleDB:
        db = DatabaseFactory::CreateOLEDBDatabase();
        break;
    default:
        db = DatabaseFactory::CreateODBCDatabase();
        break;
    }

    // 연결
    DatabaseConfig cfg;
    cfg.mConnectionString = connStr;
    try
    {
        db->Connect(cfg);
        std::cout << CLR_GREEN "연결 성공." CLR_RESET "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << CLR_RED "[FATAL] 연결 실패: " << e.what() << CLR_RESET "\n";
        return false;
    }

    auto RunTest = [](const char *name, std::function<void()> fn)
    {
        try { fn(); }
        catch (const std::exception &e)
        {
            std::cout << CLR_RED "  [ERROR] " CLR_RESET << name << ": " << e.what() << "\n";
            ++gFail;
        }
    };

    RunTest("T01", [&]() { T01_TypeRoundTrip(*db, sql); });
    RunTest("T02", [&]() { T02_IsNullThenGetString(*db, sql); });
    RunTest("T03", [&]() { T03_CaseInsensitiveColumn(*db, sql); });
    if (isSQLite)
        RunTest("T04", [&]() { T04_GetBeforeNext_SQLite(*db, sql); });
    RunTest("T05", [&]() { T05_ConnectionTransaction(*db, sql, connStr); });
    if (!isSQLite)
        RunTest("T06", [&]() { T06_DatabaseTransactionThrows(*db); });

    // 정리
    try { Exec(*db, sql.drop); } catch (...) {}
    db->Disconnect();

    return (gFail == 0);
}

// ── 입력 헬퍼 ─────────────────────────────────────────────────────────────

static std::string ReadLine(const std::string &prompt)
{
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    return line;
}

static std::string TrimStr(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string ToLowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

// ── 메인 메뉴 ─────────────────────────────────────────────────────────────

struct MenuItem
{
    Backend     backend;
    const char *label;
    const char *envHint;
};

static const MenuItem kMenuItems[] = {
    { Backend::SQLite,     "SQLite (인메모리, 즉시 사용 가능)",     ""               },
    { Backend::MSSQL_ODBC, "MSSQL  ODBC (SQL Server)",              "DB_MSSQL_ODBC"  },
    { Backend::PgSQL_ODBC, "PostgreSQL ODBC",                        "DB_PGSQL_ODBC"  },
    { Backend::MySQL_ODBC, "MySQL  ODBC",                            "DB_MYSQL_ODBC"  },
    { Backend::OleDB,      "OLE DB (SQL Server, SQLOLEDB 공급자)",   "DB_OLEDB"       },
};
static constexpr int kMenuCount = static_cast<int>(sizeof(kMenuItems) / sizeof(kMenuItems[0]));

static const MenuItem *FindMenuItem(Backend backend)
{
    for (int i = 0; i < kMenuCount; ++i)
    {
        if (kMenuItems[i].backend == backend)
            return &kMenuItems[i];
    }
    return nullptr;
}

static bool TryParseBackendName(const std::string &name, Backend &out)
{
    const std::string key = ToLowerAscii(name);
    if (key == "sqlite") { out = Backend::SQLite; return true; }
    if (key == "mssql")  { out = Backend::MSSQL_ODBC; return true; }
    if (key == "pgsql" || key == "postgres" || key == "postgresql")
    {
        out = Backend::PgSQL_ODBC;
        return true;
    }
    if (key == "mysql")  { out = Backend::MySQL_ODBC; return true; }
    if (key == "oledb")  { out = Backend::OleDB; return true; }
    return false;
}

static void PrintCliUsage(const char *programName)
{
    std::cout << "Usage: " << programName
              << " [--backend sqlite|mssql|pgsql|mysql|oledb] [--connstr <string>]\n";
}

int main(int argc, char *argv[])
{
    EnableAnsi();

    bool        cliMode = false;
    Backend     cliBackend = Backend::SQLite;
    std::string cliConnStr;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")
        {
            PrintCliUsage(argv[0]);
            return 0;
        }
        if (arg == "--backend" && i + 1 < argc)
        {
            if (!TryParseBackendName(argv[++i], cliBackend))
            {
                std::cerr << "Unknown backend: " << argv[i] << "\n";
                PrintCliUsage(argv[0]);
                return 2;
            }
            cliMode = true;
        }
        else if (arg == "--connstr" && i + 1 < argc)
        {
            cliConnStr = argv[++i];
        }
        else
        {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintCliUsage(argv[0]);
            return 2;
        }
    }

    if (cliMode)
    {
        const MenuItem *item = FindMenuItem(cliBackend);
        if (!item)
        {
            std::cerr << "Internal error: backend metadata missing\n";
            return 2;
        }

        const std::string connStr = cliConnStr.empty() ? DefaultConnStr(cliBackend) : cliConnStr;

        std::cout << "\n" CLR_MAG "=== " << item->label << " 테스트 시작 ===" CLR_RESET "\n";
        std::cout << "연결: " << connStr << "\n";

        ResetCounters();
        const bool ok = RunAllTests(cliBackend, connStr);

        std::cout << "\n" CLR_MAG "=== 결과 ===" CLR_RESET "\n";
        if (ok)
        {
            std::cout << CLR_GREEN
                << gPass << " passed, " << gFail << " failed  ?? ALL OK"
                << CLR_RESET "\n";
        }
        else
        {
            std::cout << CLR_RED
                << gPass << " passed, " << gFail << " FAILED"
                << CLR_RESET "\n";
        }
        return ok ? 0 : 1;
    }

    std::cout << CLR_MAG
        "╔══════════════════════════════════════════════════════╗\n"
        "║        Network::Database 모듈 수동 테스트             ║\n"
        "║        (DBModuleTest — db_functional_test 포트)       ║\n"
        "╚══════════════════════════════════════════════════════╝"
        CLR_RESET "\n\n";

    std::cout << "환경변수로 기본 연결 문자열을 미리 지정할 수 있습니다:\n";
    for (int i = 0; i < kMenuCount; ++i)
    {
        if (kMenuItems[i].envHint[0])
            std::cout << "  " << kMenuItems[i].envHint << "=<연결 문자열>\n";
    }
    std::cout << "\n";

    while (true)
    {
        // ── 백엔드 선택 메뉴 ──────────────────────────────────────────────
        std::cout << CLR_MAG "=== 백엔드 선택 ===" CLR_RESET "\n";
        for (int i = 0; i < kMenuCount; ++i)
            std::cout << "  " << (i + 1) << ". " << kMenuItems[i].label << "\n";
        std::cout << "  0. 종료\n\n";

        std::string choice = TrimStr(ReadLine("선택 [0-" + std::to_string(kMenuCount) + "]: "));
        if (choice == "0" || choice == "q" || choice == "Q") break;

        int idx = std::atoi(choice.c_str()) - 1;
        if (idx < 0 || idx >= kMenuCount)
        {
            std::cout << CLR_YELLOW "올바른 번호를 입력하세요.\n" CLR_RESET "\n";
            continue;
        }

        const MenuItem &item = kMenuItems[idx];
        Backend backend      = item.backend;

        // ── 연결 문자열 확인 ──────────────────────────────────────────────
        std::string defaultConn = DefaultConnStr(backend);
        std::string connStr;

        if (backend == Backend::SQLite)
        {
            connStr = defaultConn; // 항상 :memory:
            std::cout << "\nSQLite 인메모리 DB 사용.\n";
        }
        else
        {
            std::cout << "\n기본 연결 문자열:\n  " << CLR_CYAN << defaultConn << CLR_RESET "\n";
            std::string input = TrimStr(ReadLine(
                "연결 문자열 입력 (Enter = 기본값 사용): "));
            connStr = input.empty() ? defaultConn : input;
        }

        // ── 테스트 실행 ───────────────────────────────────────────────────
        std::cout << "\n" CLR_MAG "=== " << item.label << " 테스트 시작 ===" CLR_RESET "\n";
        std::cout << "연결: " << connStr << "\n";

        ResetCounters();
        bool ok = RunAllTests(backend, connStr);

        // ── 결과 요약 ─────────────────────────────────────────────────────
        std::cout << "\n" CLR_MAG "=== 결과 ===" CLR_RESET "\n";
        if (ok)
        {
            std::cout << CLR_GREEN
                << gPass << " passed, " << gFail << " failed  ✓  ALL OK"
                << CLR_RESET "\n";
        }
        else
        {
            std::cout << CLR_RED
                << gPass << " passed, " << gFail << " FAILED"
                << CLR_RESET "\n";
        }

        std::cout << "\n";
        ReadLine("Enter를 눌러 계속...");
        std::cout << "\n";
    }

    std::cout << "종료합니다.\n";
    return 0;
}
