// English: SQLiteDatabase implementation (compiled only when HAVE_SQLITE3 is defined)
// 한글: SQLiteDatabase 구현 (HAVE_SQLITE3 정의 시에만 컴파일)

#include "SQLiteDatabase.h"
#include "Utils/Logger.h"

#ifdef HAVE_SQLITE3

#include <sstream>

namespace Network
{
namespace Database
{

// =============================================================================
// English: SQLiteDatabase Implementation
// 한글: SQLiteDatabase 구현
// =============================================================================

SQLiteDatabase::SQLiteDatabase() : mDb(nullptr), mConnected(false) {}

SQLiteDatabase::~SQLiteDatabase()
{
	Disconnect();
}

void SQLiteDatabase::Connect(const DatabaseConfig &config)
{
	mConfig = config;

	// English: mConnectionString is the SQLite file path (use ":memory:" for in-memory)
	// 한글: mConnectionString을 SQLite 파일 경로로 사용 (":memory:"는 인메모리)
	int rc = sqlite3_open(config.mConnectionString.c_str(), &mDb);
	if (rc != SQLITE_OK)
	{
		std::string err = mDb ? sqlite3_errmsg(mDb) : "unknown error";
		sqlite3_close(mDb);
		mDb = nullptr;
		throw DatabaseException("SQLite open failed: " + err, rc);
	}

	// English: Enable WAL mode for better concurrent access
	// 한글: 동시 접근 향상을 위한 WAL 모드 활성화
	char *walErrMsg = nullptr;
	int walRc = sqlite3_exec(mDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &walErrMsg);
	if (walRc != SQLITE_OK)
	{
		std::string walErr = walErrMsg ? walErrMsg : "unknown error";
		sqlite3_free(walErrMsg);
		Logger::Warn("SQLiteDatabase: Failed to set WAL mode: " + walErr);
	}

	mConnected = true;
}

void SQLiteDatabase::Disconnect()
{
	if (mDb)
	{
		sqlite3_close(mDb);
		mDb = nullptr;
	}
	mConnected = false;
}

bool SQLiteDatabase::IsConnected() const
{
	return mConnected;
}

std::unique_ptr<IConnection> SQLiteDatabase::CreateConnection()
{
	if (!mConnected)
	{
		throw DatabaseException("SQLiteDatabase not connected");
	}
	auto conn = std::make_unique<SQLiteConnection>(mDb);
	conn->Open("");
	return conn;
}

std::unique_ptr<IStatement> SQLiteDatabase::CreateStatement()
{
	if (!mConnected)
	{
		throw DatabaseException("SQLiteDatabase not connected");
	}
	return std::make_unique<SQLiteStatement>(mDb);
}

void SQLiteDatabase::BeginTransaction()
{
	ExecRaw("BEGIN TRANSACTION");
}

void SQLiteDatabase::CommitTransaction()
{
	ExecRaw("COMMIT");
}

void SQLiteDatabase::RollbackTransaction()
{
	ExecRaw("ROLLBACK");
}

void SQLiteDatabase::ExecRaw(const char *sql)
{
	if (!mDb)
		return;
	char *errMsg = nullptr;
	int rc = sqlite3_exec(mDb, sql, nullptr, nullptr, &errMsg);
	if (rc != SQLITE_OK)
	{
		std::string err = errMsg ? errMsg : "unknown error";
		sqlite3_free(errMsg);
		throw DatabaseException(std::string("SQLite exec failed: ") + err, rc);
	}
}

// =============================================================================
// English: SQLiteConnection Implementation
// 한글: SQLiteConnection 구현
// =============================================================================

SQLiteConnection::SQLiteConnection(sqlite3 *db)
	: mDb(db), mOpen(false), mInTransaction(false), mLastErrorCode(0)
{
}

std::unique_ptr<IStatement> SQLiteConnection::CreateStatement()
{
	if (!mOpen)
	{
		throw DatabaseException("SQLiteConnection not open");
	}
	return std::make_unique<SQLiteStatement>(mDb);
}

void SQLiteConnection::BeginTransaction()
{
	ExecRaw("BEGIN TRANSACTION");
	mInTransaction = true;
}

void SQLiteConnection::CommitTransaction()
{
	ExecRaw("COMMIT");
	mInTransaction = false;
}

void SQLiteConnection::RollbackTransaction()
{
	ExecRaw("ROLLBACK");
	mInTransaction = false;
}

void SQLiteConnection::ExecRaw(const char *sql)
{
	if (!mDb)
		return;
	char *errMsg = nullptr;
	int rc = sqlite3_exec(mDb, sql, nullptr, nullptr, &errMsg);
	mLastErrorCode = rc;
	if (rc != SQLITE_OK)
	{
		mLastError = errMsg ? errMsg : "unknown error";
		sqlite3_free(errMsg);
		throw DatabaseException(std::string("SQLiteConnection exec failed: ") + mLastError, rc);
	}
}

// =============================================================================
// English: SQLiteResultSet Implementation
// 한글: SQLiteResultSet 구현
// =============================================================================

SQLiteResultSet::SQLiteResultSet(sqlite3_stmt *stmt)
	: mStmt(stmt), mDone(false)
{
	if (mStmt)
	{
		LoadColumnNames();
	}
}

SQLiteResultSet::~SQLiteResultSet()
{
	Close();
}

void SQLiteResultSet::LoadColumnNames()
{
	int count = sqlite3_column_count(mStmt);
	mColumnNames.resize(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		const char *name = sqlite3_column_name(mStmt, i);
		mColumnNames[static_cast<size_t>(i)] = name ? name : "";
	}
}

bool SQLiteResultSet::Next()
{
	if (mDone || !mStmt)
		return false;

	int rc = sqlite3_step(mStmt);
	if (rc == SQLITE_ROW)
		return true;

	mDone = true;
	return false;
}

bool SQLiteResultSet::IsNull(size_t columnIndex)
{
	if (!mStmt)
		return true;
	return sqlite3_column_type(mStmt, static_cast<int>(columnIndex)) == SQLITE_NULL;
}

bool SQLiteResultSet::IsNull(const std::string &columnName)
{
	return IsNull(static_cast<size_t>(ResolveColumn(columnName)));
}

std::string SQLiteResultSet::GetString(size_t columnIndex)
{
	if (!mStmt)
		return {};
	const unsigned char *text = sqlite3_column_text(mStmt, static_cast<int>(columnIndex));
	return text ? reinterpret_cast<const char *>(text) : "";
}

std::string SQLiteResultSet::GetString(const std::string &columnName)
{
	return GetString(static_cast<size_t>(ResolveColumn(columnName)));
}

int SQLiteResultSet::GetInt(size_t columnIndex)
{
	if (!mStmt)
		return 0;
	return sqlite3_column_int(mStmt, static_cast<int>(columnIndex));
}

int SQLiteResultSet::GetInt(const std::string &columnName)
{
	return GetInt(static_cast<size_t>(ResolveColumn(columnName)));
}

long long SQLiteResultSet::GetLong(size_t columnIndex)
{
	if (!mStmt)
		return 0;
	return sqlite3_column_int64(mStmt, static_cast<int>(columnIndex));
}

long long SQLiteResultSet::GetLong(const std::string &columnName)
{
	return GetLong(static_cast<size_t>(ResolveColumn(columnName)));
}

double SQLiteResultSet::GetDouble(size_t columnIndex)
{
	if (!mStmt)
		return 0.0;
	return sqlite3_column_double(mStmt, static_cast<int>(columnIndex));
}

double SQLiteResultSet::GetDouble(const std::string &columnName)
{
	return GetDouble(static_cast<size_t>(ResolveColumn(columnName)));
}

bool SQLiteResultSet::GetBool(size_t columnIndex)
{
	return GetInt(columnIndex) != 0;
}

bool SQLiteResultSet::GetBool(const std::string &columnName)
{
	return GetBool(static_cast<size_t>(ResolveColumn(columnName)));
}

size_t SQLiteResultSet::GetColumnCount() const
{
	return mColumnNames.size();
}

std::string SQLiteResultSet::GetColumnName(size_t columnIndex) const
{
	if (columnIndex >= mColumnNames.size())
		throw DatabaseException("SQLiteResultSet: column index out of range");
	return mColumnNames[columnIndex];
}

size_t SQLiteResultSet::FindColumn(const std::string &columnName) const
{
	return static_cast<size_t>(ResolveColumn(columnName));
}

void SQLiteResultSet::Close()
{
	if (mStmt)
	{
		sqlite3_finalize(mStmt);
		mStmt = nullptr;
	}
}

int SQLiteResultSet::ResolveColumn(const std::string &columnName) const
{
	for (size_t i = 0; i < mColumnNames.size(); ++i)
	{
		if (mColumnNames[i] == columnName)
			return static_cast<int>(i);
	}
	throw DatabaseException("SQLiteResultSet: column not found: " + columnName);
}

// =============================================================================
// English: SQLiteStatement Implementation
// 한글: SQLiteStatement 구현
// =============================================================================

SQLiteStatement::SQLiteStatement(sqlite3 *db) : mDb(db), mStmt(nullptr) {}

SQLiteStatement::~SQLiteStatement()
{
	Close();
}

void SQLiteStatement::SetQuery(const std::string &query)
{
	mQuery = query;
	if (mStmt)
	{
		sqlite3_finalize(mStmt);
		mStmt = nullptr;
	}
}

void SQLiteStatement::BindParameter(size_t index, const std::string &value)
{
	if (mCurrentParams.size() < index)
		mCurrentParams.resize(index);
	Param p;
	p.type = ParamType::Text;
	p.text = value;
	mCurrentParams[index - 1] = p;
}

void SQLiteStatement::BindParameter(size_t index, int value)
{
	if (mCurrentParams.size() < index)
		mCurrentParams.resize(index);
	Param p;
	p.type = ParamType::Int;
	p.int64Val = value;
	mCurrentParams[index - 1] = p;
}

void SQLiteStatement::BindParameter(size_t index, long long value)
{
	if (mCurrentParams.size() < index)
		mCurrentParams.resize(index);
	Param p;
	p.type = ParamType::Int64;
	p.int64Val = value;
	mCurrentParams[index - 1] = p;
}

void SQLiteStatement::BindParameter(size_t index, double value)
{
	if (mCurrentParams.size() < index)
		mCurrentParams.resize(index);
	Param p;
	p.type = ParamType::Real;
	p.realVal = value;
	mCurrentParams[index - 1] = p;
}

void SQLiteStatement::BindParameter(size_t index, bool value)
{
	BindParameter(index, static_cast<int>(value ? 1 : 0));
}

void SQLiteStatement::BindNullParameter(size_t index)
{
	if (mCurrentParams.size() < index)
		mCurrentParams.resize(index);
	Param p;
	p.type = ParamType::Null;
	mCurrentParams[index - 1] = p;
}

sqlite3_stmt *SQLiteStatement::PrepareStmt()
{
	sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(mDb, mQuery.c_str(), -1, &stmt, nullptr);
	if (rc != SQLITE_OK)
	{
		throw DatabaseException(
			std::string("SQLite prepare failed: ") + sqlite3_errmsg(mDb), rc);
	}
	return stmt;
}

void SQLiteStatement::BindAll(sqlite3_stmt *stmt, const std::vector<Param> &params)
{
	for (size_t i = 0; i < params.size(); ++i)
	{
		const auto &p = params[i];
		int col = static_cast<int>(i) + 1;
		int rc = SQLITE_OK;
		switch (p.type)
		{
		case ParamType::Text:
			rc = sqlite3_bind_text(stmt, col, p.text.c_str(), -1, SQLITE_TRANSIENT);
			break;
		case ParamType::Int:
		case ParamType::Int64:
			rc = sqlite3_bind_int64(stmt, col, p.int64Val);
			break;
		case ParamType::Real:
			rc = sqlite3_bind_double(stmt, col, p.realVal);
			break;
		case ParamType::Null:
		default:
			rc = sqlite3_bind_null(stmt, col);
			break;
		}
		if (rc != SQLITE_OK)
		{
			throw DatabaseException(
				std::string("SQLite bind failed: ") + sqlite3_errmsg(mDb), rc);
		}
	}
}

void SQLiteStatement::CheckRC(int rc, const char *op) const
{
	if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE)
	{
		throw DatabaseException(
			std::string(op) + ": " + sqlite3_errmsg(mDb), rc);
	}
}

std::unique_ptr<IResultSet> SQLiteStatement::ExecuteQuery()
{
	sqlite3_stmt *stmt = PrepareStmt();
	BindAll(stmt, mCurrentParams);
	// English: Ownership of stmt is transferred to SQLiteResultSet
	// 한글: stmt 소유권을 SQLiteResultSet으로 이전
	return std::make_unique<SQLiteResultSet>(stmt);
}

int SQLiteStatement::ExecuteUpdate()
{
	sqlite3_stmt *stmt = PrepareStmt();
	BindAll(stmt, mCurrentParams);

	int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE)
	{
		throw DatabaseException(
			std::string("SQLite ExecuteUpdate failed: ") + sqlite3_errmsg(mDb), rc);
	}
	return sqlite3_changes(mDb);
}

bool SQLiteStatement::Execute()
{
	sqlite3_stmt *stmt = PrepareStmt();
	BindAll(stmt, mCurrentParams);

	int rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE && rc != SQLITE_ROW)
	{
		throw DatabaseException(
			std::string("SQLite Execute failed: ") + sqlite3_errmsg(mDb), rc);
	}
	return true;
}

void SQLiteStatement::AddBatch()
{
	mBatchParams.push_back(mCurrentParams);
	mCurrentParams.clear();
}

std::vector<int> SQLiteStatement::ExecuteBatch()
{
	std::vector<int> results;
	results.reserve(mBatchParams.size());

	for (auto &paramSet : mBatchParams)
	{
		sqlite3_stmt *stmt = PrepareStmt();
		BindAll(stmt, paramSet);
		int rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);

		if (rc == SQLITE_DONE)
		{
			results.push_back(sqlite3_changes(mDb));
		}
		else
		{
			results.push_back(-1);
		}
	}

	mBatchParams.clear();
	return results;
}

void SQLiteStatement::ClearParameters()
{
	mCurrentParams.clear();
}

void SQLiteStatement::Close()
{
	if (mStmt)
	{
		sqlite3_finalize(mStmt);
		mStmt = nullptr;
	}
}

} // namespace Database
} // namespace Network

#endif // HAVE_SQLITE3
