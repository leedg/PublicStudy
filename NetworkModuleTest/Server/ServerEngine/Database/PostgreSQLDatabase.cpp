// PostgreSQLDatabase 구현 (HAVE_LIBPQ 정의 시에만 컴파일)

#include "PostgreSQLDatabase.h"
#include "Utils/Logger.h"

#ifdef HAVE_LIBPQ

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace Network
{
namespace Database
{

// =============================================================================
// Helpers
// =============================================================================

namespace
{

// Throws DatabaseException with the connection's error message.
void ThrowConnError(PGconn *conn, const std::string &context)
{
	const char *msg = PQerrorMessage(conn);
	throw DatabaseException(context + ": " + (msg ? msg : "unknown error"));
}

// Throws DatabaseException with the result's error message, then clears result.
void ThrowResultError(PGresult *result, const std::string &context)
{
	const char *msg = PQresultErrorMessage(result);
	std::string err = context + ": " + (msg ? msg : "unknown error");
	PQclear(result);
	throw DatabaseException(err);
}

} // namespace

// =============================================================================
// PostgreSQLResultSet
// =============================================================================

PostgreSQLResultSet::PostgreSQLResultSet(PGresult *result)
	: mResult(result)
	, mNumRows(result ? PQntuples(result) : 0)
	, mCurrentRow(-1)
{
	if (mResult)
	{
		int nFields = PQnfields(mResult);
		mColumnNames.resize(static_cast<size_t>(nFields));
		for (int i = 0; i < nFields; ++i)
		{
			const char *name = PQfname(mResult, i);
			mColumnNames[static_cast<size_t>(i)] = name ? name : "";
		}
	}
}

PostgreSQLResultSet::~PostgreSQLResultSet()
{
	Close();
}

void PostgreSQLResultSet::Close()
{
	if (mResult)
	{
		PQclear(mResult);
		mResult = nullptr;
	}
}

bool PostgreSQLResultSet::Next()
{
	if (!mResult)
		return false;
	++mCurrentRow;
	return mCurrentRow < mNumRows;
}

void PostgreSQLResultSet::CheckRow() const
{
	if (!mResult || mCurrentRow < 0 || mCurrentRow >= mNumRows)
		throw DatabaseException("PostgreSQLResultSet: no current row (call Next() first)");
}

bool PostgreSQLResultSet::IsNull(size_t columnIndex)
{
	if (!mResult || mCurrentRow < 0 || mCurrentRow >= mNumRows)
		return true;
	return PQgetisnull(mResult, mCurrentRow, static_cast<int>(columnIndex)) != 0;
}

std::string PostgreSQLResultSet::GetString(size_t columnIndex)
{
	CheckRow();
	if (PQgetisnull(mResult, mCurrentRow, static_cast<int>(columnIndex)))
		return {};
	const char *val = PQgetvalue(mResult, mCurrentRow, static_cast<int>(columnIndex));
	return val ? val : "";
}

int PostgreSQLResultSet::GetInt(size_t columnIndex)
{
	CheckRow();
	if (PQgetisnull(mResult, mCurrentRow, static_cast<int>(columnIndex)))
		return 0;
	const char *val = PQgetvalue(mResult, mCurrentRow, static_cast<int>(columnIndex));
	if (!val || *val == '\0')
		return 0;
	try { return std::stoi(val); }
	catch (...) { return 0; }
}

long long PostgreSQLResultSet::GetLong(size_t columnIndex)
{
	CheckRow();
	if (PQgetisnull(mResult, mCurrentRow, static_cast<int>(columnIndex)))
		return 0;
	const char *val = PQgetvalue(mResult, mCurrentRow, static_cast<int>(columnIndex));
	if (!val || *val == '\0')
		return 0;
	try { return std::stoll(val); }
	catch (...) { return 0; }
}

double PostgreSQLResultSet::GetDouble(size_t columnIndex)
{
	CheckRow();
	if (PQgetisnull(mResult, mCurrentRow, static_cast<int>(columnIndex)))
		return 0.0;
	const char *val = PQgetvalue(mResult, mCurrentRow, static_cast<int>(columnIndex));
	if (!val || *val == '\0')
		return 0.0;
	try { return std::stod(val); }
	catch (...) { return 0.0; }
}

bool PostgreSQLResultSet::GetBool(size_t columnIndex)
{
	CheckRow();
	if (PQgetisnull(mResult, mCurrentRow, static_cast<int>(columnIndex)))
		return false;
	const char *val = PQgetvalue(mResult, mCurrentRow, static_cast<int>(columnIndex));
	if (!val || *val == '\0')
		return false;
	// PostgreSQL BOOLEAN 컬럼은 't'/'f'를 반환한다. '1'/'T'도 허용.
	return (*val == 't' || *val == 'T' || *val == '1');
}

size_t PostgreSQLResultSet::GetColumnCount() const
{
	return mColumnNames.size();
}

std::string PostgreSQLResultSet::GetColumnName(size_t columnIndex) const
{
	if (columnIndex >= mColumnNames.size())
		throw DatabaseException("PostgreSQLResultSet: column index out of range");
	return mColumnNames[columnIndex];
}

size_t PostgreSQLResultSet::FindColumn(const std::string &columnName) const
{
	auto iequal = [](unsigned char a, unsigned char b) {
		return std::tolower(a) == std::tolower(b);
	};
	for (size_t i = 0; i < mColumnNames.size(); ++i)
	{
		const auto &name = mColumnNames[i];
		if (name.size() == columnName.size() &&
		    std::equal(name.begin(), name.end(), columnName.begin(), iequal))
		{
			return i;
		}
	}
	throw DatabaseException("PostgreSQLResultSet: column not found: " + columnName);
}

// =============================================================================
// PostgreSQLStatement
// =============================================================================

PostgreSQLStatement::PostgreSQLStatement(PGconn *conn)
	: mConn(conn)
{
}

PostgreSQLStatement::~PostgreSQLStatement()
{
	Close();
}

void PostgreSQLStatement::Close()
{
	mQuery.clear();
	mCurrentParams.clear();
	mBatchParams.clear();
}

void PostgreSQLStatement::SetQuery(const std::string &query)
{
	mQuery = query;
	mCurrentParams.clear();
}

void PostgreSQLStatement::SetTimeout(int seconds)
{
	mTimeoutSeconds = seconds;
}

void PostgreSQLStatement::EnsureQuery() const
{
	if (mQuery.empty())
		throw DatabaseException("PostgreSQLStatement: no query set (call SetQuery first)");
}

void PostgreSQLStatement::SetParam(size_t index, std::string value)
{
	if (mCurrentParams.size() < index)
		mCurrentParams.resize(index);
	auto &p   = mCurrentParams[index - 1];
	p.isNull  = false;
	p.value   = std::move(value);
}

void PostgreSQLStatement::BindParameter(size_t index, const std::string &value)
{
	SetParam(index, value);
}

void PostgreSQLStatement::BindParameter(size_t index, int value)
{
	SetParam(index, std::to_string(value));
}

void PostgreSQLStatement::BindParameter(size_t index, long long value)
{
	SetParam(index, std::to_string(value));
}

void PostgreSQLStatement::BindParameter(size_t index, double value)
{
	SetParam(index, std::to_string(value));
}

void PostgreSQLStatement::BindParameter(size_t index, bool value)
{
	SetParam(index, value ? "t" : "f");
}

void PostgreSQLStatement::BindNullParameter(size_t index)
{
	if (mCurrentParams.size() < index)
		mCurrentParams.resize(index);
	mCurrentParams[index - 1] = Param{}; // isNull = true
}

void PostgreSQLStatement::ApplyTimeout() const
{
	if (mTimeoutSeconds <= 0)
		return;
	const std::string sql =
		"SET statement_timeout = " + std::to_string(mTimeoutSeconds * 1000);
	PGresult *res = PQexec(mConn, sql.c_str());
	PQclear(res); // best-effort; ignore errors
}

PGresult *PostgreSQLStatement::RunExecParams(const std::vector<Param> &params)
{
	ApplyTimeout();

	std::vector<const char *> values;
	values.reserve(params.size());
	for (const auto &p : params)
		values.push_back(p.isNull ? nullptr : p.value.c_str());

	return PQexecParams(
		mConn,
		mQuery.c_str(),
		static_cast<int>(values.size()),
		nullptr,                            // paramTypes: 서버가 타입 추론
		values.empty() ? nullptr : values.data(),
		nullptr,                            // paramLengths: 텍스트 형식이므로 미사용
		nullptr,                            // paramFormats: 모두 텍스트(0)
		0                                   // resultFormat: 텍스트
	);
}

int PostgreSQLStatement::ParseAffectedRows(PGresult *result)
{
	const char *rows = PQcmdTuples(result);
	if (!rows || *rows == '\0')
		return 0;
	try { return std::stoi(rows); }
	catch (...) { return 0; }
}

std::unique_ptr<IResultSet> PostgreSQLStatement::ExecuteQuery()
{
	EnsureQuery();
	PGresult *result = RunExecParams(mCurrentParams);
	if (PQresultStatus(result) != PGRES_TUPLES_OK)
		ThrowResultError(result, "PostgreSQLStatement::ExecuteQuery");
	return std::make_unique<PostgreSQLResultSet>(result);
}

int PostgreSQLStatement::ExecuteUpdate()
{
	EnsureQuery();
	PGresult *result = RunExecParams(mCurrentParams);
	if (PQresultStatus(result) != PGRES_COMMAND_OK)
		ThrowResultError(result, "PostgreSQLStatement::ExecuteUpdate");
	const int affected = ParseAffectedRows(result);
	PQclear(result);
	return affected;
}

bool PostgreSQLStatement::Execute()
{
	EnsureQuery();
	PGresult *result = RunExecParams(mCurrentParams);
	const ExecStatusType status = PQresultStatus(result);
	if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
		ThrowResultError(result, "PostgreSQLStatement::Execute");
	PQclear(result);
	return true;
}

void PostgreSQLStatement::AddBatch()
{
	mBatchParams.push_back(mCurrentParams);
	mCurrentParams.clear();
}

std::vector<int> PostgreSQLStatement::ExecuteBatch()
{
	EnsureQuery();
	std::vector<int> results;
	results.reserve(mBatchParams.size());

	for (const auto &paramSet : mBatchParams)
	{
		PGresult *result = RunExecParams(paramSet);
		const ExecStatusType status = PQresultStatus(result);
		if (status == PGRES_COMMAND_OK)
		{
			results.push_back(ParseAffectedRows(result));
		}
		else
		{
			const char *msg = PQresultErrorMessage(result);
			Utils::Logger::Warn(
				std::string("PostgreSQLStatement::ExecuteBatch row failed: ") +
				(msg ? msg : "unknown"));
			results.push_back(-1);
		}
		PQclear(result);
	}

	mBatchParams.clear();
	return results;
}

void PostgreSQLStatement::ClearParameters()
{
	mCurrentParams.clear();
}

// =============================================================================
// PostgreSQLConnection
// =============================================================================

PostgreSQLConnection::PostgreSQLConnection() = default;

PostgreSQLConnection::~PostgreSQLConnection()
{
	Close();
}

void PostgreSQLConnection::Open(const std::string &connectionString)
{
	if (mConn)
		Close();

	mConn = PQconnectdb(connectionString.c_str());
	if (!mConn || PQstatus(mConn) != CONNECTION_OK)
	{
		std::string err = mConn ? PQerrorMessage(mConn) : "PQconnectdb returned null";
		if (mConn)
		{
			PQfinish(mConn);
			mConn = nullptr;
		}
		throw DatabaseException("PostgreSQLConnection::Open failed: " + err);
	}
}

void PostgreSQLConnection::Close()
{
	if (mConn)
	{
		if (mInTransaction)
		{
			PGresult *res = PQexec(mConn, "ROLLBACK");
			PQclear(res);
			mInTransaction = false;
		}
		PQfinish(mConn);
		mConn = nullptr;
	}
}

bool PostgreSQLConnection::IsOpen() const
{
	return mConn && PQstatus(mConn) == CONNECTION_OK;
}

std::unique_ptr<IStatement> PostgreSQLConnection::CreateStatement()
{
	if (!IsOpen())
		throw DatabaseException("PostgreSQLConnection not open");
	return std::make_unique<PostgreSQLStatement>(mConn);
}

void PostgreSQLConnection::ExecRaw(const char *sql)
{
	PGresult *res = PQexec(mConn, sql);
	const ExecStatusType status = PQresultStatus(res);
	if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
	{
		const char *msg = PQresultErrorMessage(res);
		mLastError     = msg ? msg : "unknown error";
		mLastErrorCode = -1;
		PQclear(res);
		throw DatabaseException(
			std::string("PostgreSQLConnection exec failed: ") + mLastError);
	}
	mLastErrorCode = 0;
	mLastError.clear();
	PQclear(res);
}

void PostgreSQLConnection::BeginTransaction()
{
	ExecRaw("BEGIN");
	mInTransaction = true;
}

void PostgreSQLConnection::CommitTransaction()
{
	ExecRaw("COMMIT");
	mInTransaction = false;
}

void PostgreSQLConnection::RollbackTransaction()
{
	ExecRaw("ROLLBACK");
	mInTransaction = false;
}

int PostgreSQLConnection::GetLastErrorCode() const
{
	return mLastErrorCode;
}

std::string PostgreSQLConnection::GetLastError() const
{
	return mLastError;
}

// =============================================================================
// PostgreSQLDatabase
// =============================================================================

PostgreSQLDatabase::PostgreSQLDatabase() = default;

PostgreSQLDatabase::~PostgreSQLDatabase()
{
	Disconnect();
}

void PostgreSQLDatabase::Connect(const DatabaseConfig &config)
{
	mConfig = config;

	mConn = PQconnectdb(config.mConnectionString.c_str());
	if (!mConn || PQstatus(mConn) != CONNECTION_OK)
	{
		std::string err = mConn ? PQerrorMessage(mConn) : "PQconnectdb returned null";
		if (mConn)
		{
			PQfinish(mConn);
			mConn = nullptr;
		}
		throw DatabaseException("PostgreSQLDatabase::Connect failed: " + err);
	}

	mConnected = true;
}

void PostgreSQLDatabase::Disconnect()
{
	if (mConn)
	{
		PQfinish(mConn);
		mConn = nullptr;
	}
	mConnected = false;
}

bool PostgreSQLDatabase::IsConnected() const
{
	return mConnected && mConn && PQstatus(mConn) == CONNECTION_OK;
}

std::unique_ptr<IConnection> PostgreSQLDatabase::CreateConnection()
{
	if (!mConnected)
		throw DatabaseException("PostgreSQLDatabase not connected");

	auto conn = std::make_unique<PostgreSQLConnection>();
	conn->Open(mConfig.mConnectionString);
	return conn;
}

std::unique_ptr<IStatement> PostgreSQLDatabase::CreateStatement()
{
	if (!mConnected || !mConn)
		throw DatabaseException("PostgreSQLDatabase not connected");
	return std::make_unique<PostgreSQLStatement>(mConn);
}

void PostgreSQLDatabase::ExecRaw(const char *sql)
{
	if (!mConn)
		return;
	PGresult *res = PQexec(mConn, sql);
	const ExecStatusType status = PQresultStatus(res);
	if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
	{
		const char *msg = PQresultErrorMessage(res);
		std::string err = msg ? msg : "unknown error";
		PQclear(res);
		throw DatabaseException(std::string("PostgreSQLDatabase exec failed: ") + err);
	}
	PQclear(res);
}

void PostgreSQLDatabase::BeginTransaction()
{
	ExecRaw("BEGIN");
}

void PostgreSQLDatabase::CommitTransaction()
{
	ExecRaw("COMMIT");
}

void PostgreSQLDatabase::RollbackTransaction()
{
	ExecRaw("ROLLBACK");
}

} // namespace Database
} // namespace Network

#endif // HAVE_LIBPQ
