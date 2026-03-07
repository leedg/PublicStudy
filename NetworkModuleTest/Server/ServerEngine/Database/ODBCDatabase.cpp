// English: ODBCDatabase implementation
// 한글: ODBCDatabase 구현

#include "ODBCDatabase.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace Network
{
namespace Database
{

// =============================================================================
// English: ODBCDatabase Implementation
// 한글: ODBCDatabase 구현
// =============================================================================

ODBCDatabase::ODBCDatabase() : mEnvironment(SQL_NULL_HANDLE), mConnected(false)
{
	InitializeEnvironment();
}

ODBCDatabase::~ODBCDatabase()
{
	Disconnect();
	CleanupEnvironment();
}

void ODBCDatabase::InitializeEnvironment()
{
	SQLRETURN ret =
		SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &mEnvironment);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		throw DatabaseException("Failed to allocate ODBC environment handle");
	}

	ret = SQLSetEnvAttr(mEnvironment, SQL_ATTR_ODBC_VERSION,
						(SQLPOINTER)SQL_OV_ODBC3, 0);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		SQLFreeHandle(SQL_HANDLE_ENV, mEnvironment);
		mEnvironment = SQL_NULL_HANDLE;
		throw DatabaseException("Failed to set ODBC version");
	}
}

void ODBCDatabase::CleanupEnvironment()
{
	if (mEnvironment != SQL_NULL_HANDLE)
	{
		SQLFreeHandle(SQL_HANDLE_ENV, mEnvironment);
		mEnvironment = SQL_NULL_HANDLE;
	}
}

void ODBCDatabase::CheckSQLReturn(SQLRETURN ret, const std::string &operation,
								  SQLHANDLE handle, SQLSMALLINT handleType)
{
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		SQLCHAR sqlState[6];
		SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
		SQLINTEGER nativeError;
		SQLSMALLINT messageLength;

		SQLGetDiagRecA(handleType, handle, 1, sqlState, &nativeError, message,
						   SQL_MAX_MESSAGE_LENGTH, &messageLength);

		std::ostringstream oss;
		oss << operation << " failed: " << reinterpret_cast<char *>(message)
			<< " (SQL State: " << reinterpret_cast<char *>(sqlState) << ")";
		throw DatabaseException(oss.str(), static_cast<int>(nativeError));
	}
}

void ODBCDatabase::Connect(const DatabaseConfig &config)
{
	mConfig = config;
	// English: Open a temporary connection to validate the connection string,
	//          then discard it. Real connections are obtained via CreateConnection().
	// 한글: 연결 문자열 유효성 검사를 위해 임시 연결을 열고 즉시 폐기.
	//       실제 연결은 CreateConnection()으로 얻음.
	auto pConnection = std::make_unique<ODBCConnection>(mEnvironment);
	pConnection->Open(config.mConnectionString);
	mConnected = true;
}

void ODBCDatabase::Disconnect() { mConnected = false; }

bool ODBCDatabase::IsConnected() const { return mConnected; }

std::unique_ptr<IConnection> ODBCDatabase::CreateConnection()
{
	if (!mConnected)
	{
		throw DatabaseException("Database not connected");
	}
	return std::make_unique<ODBCConnection>(mEnvironment);
}

std::unique_ptr<IStatement> ODBCDatabase::CreateStatement()
{
	if (!mConnected)
	{
		throw DatabaseException("Database not connected");
	}
	auto pConnection = CreateConnection();
	pConnection->Open(mConfig.mConnectionString);
	return pConnection->CreateStatement();
}

void ODBCDatabase::BeginTransaction()
{
	// English: Transaction state is per-connection. Use IConnection::BeginTransaction()
	//          on a connection obtained from CreateConnection() or a ConnectionPool.
	// 한글: 트랜잭션 상태는 연결 단위. CreateConnection() 또는 ConnectionPool로
	//       얻은 연결에서 IConnection::BeginTransaction()을 사용해야 함.
	throw DatabaseException(
		"ODBCDatabase: call IConnection::BeginTransaction() on a connection from CreateConnection()");
}

void ODBCDatabase::CommitTransaction()
{
	throw DatabaseException(
		"ODBCDatabase: call IConnection::CommitTransaction() on a connection from CreateConnection()");
}

void ODBCDatabase::RollbackTransaction()
{
	throw DatabaseException(
		"ODBCDatabase: call IConnection::RollbackTransaction() on a connection from CreateConnection()");
}

// =============================================================================
// English: ODBCConnection Implementation
// 한글: ODBCConnection 구현
// =============================================================================

ODBCConnection::ODBCConnection(SQLHENV env)
	: mConnection(SQL_NULL_HANDLE), mEnvironment(env), mConnected(false),
		  mLastErrorCode(0)
{
	SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_DBC, mEnvironment, &mConnection);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		throw DatabaseException("Failed to allocate ODBC connection handle");
	}
}

ODBCConnection::~ODBCConnection() { Close(); }

void ODBCConnection::Open(const std::string &connectionString)
{
	if (mConnected)
	{
		return; // English: Already connected / 한글: 이미 연결됨
	}

	SQLCHAR connStrOut[1024];
	SQLSMALLINT connStrOutLength;

	try
	{
		SQLRETURN ret = SQLDriverConnectA(
			mConnection, nullptr, (SQLCHAR *)connectionString.c_str(), SQL_NTS,
			connStrOut, sizeof(connStrOut), &connStrOutLength, SQL_DRIVER_NOPROMPT);

		CheckSQLReturn(ret, "Connection");
		mConnected = true;
	}
	catch (const std::exception&)
	{
		if (mConnection != SQL_NULL_HANDLE)
		{
			SQLFreeHandle(SQL_HANDLE_DBC, mConnection);
			mConnection = SQL_NULL_HANDLE;
		}
		throw;
	}
}

void ODBCConnection::Close()
{
	if (mConnection != SQL_NULL_HANDLE)
	{
		if (mConnected)
		{
			SQLDisconnect(mConnection);
			mConnected = false;
		}
		SQLFreeHandle(SQL_HANDLE_DBC, mConnection);
		mConnection = SQL_NULL_HANDLE;
	}
}

bool ODBCConnection::IsOpen() const { return mConnected; }

std::unique_ptr<IStatement> ODBCConnection::CreateStatement()
{
	if (!mConnected)
	{
		throw DatabaseException("Connection not open");
	}
	return std::make_unique<ODBCStatement>(mConnection);
}

void ODBCConnection::BeginTransaction()
{
	SQLRETURN ret = SQLSetConnectAttr(mConnection, SQL_ATTR_AUTOCOMMIT,
										  (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
	CheckSQLReturn(ret, "Begin transaction");
}

void ODBCConnection::CommitTransaction()
{
	SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, mConnection, SQL_COMMIT);
	CheckSQLReturn(ret, "Commit transaction");

	ret = SQLSetConnectAttr(mConnection, SQL_ATTR_AUTOCOMMIT,
							(SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
	CheckSQLReturn(ret, "Reset autocommit");
}

void ODBCConnection::RollbackTransaction()
{
	SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, mConnection, SQL_ROLLBACK);
	CheckSQLReturn(ret, "Rollback transaction");

	ret = SQLSetConnectAttr(mConnection, SQL_ATTR_AUTOCOMMIT,
							(SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
	CheckSQLReturn(ret, "Reset autocommit");
}

void ODBCConnection::CheckSQLReturn(SQLRETURN ret, const std::string &operation)
{
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		mLastError = GetSQLErrorMessage(mConnection, SQL_HANDLE_DBC);
		throw DatabaseException(operation + ": " + mLastError);
	}
}

std::string ODBCConnection::GetSQLErrorMessage(SQLHANDLE handle,
												   SQLSMALLINT handleType)
{
	SQLCHAR sqlState[6];
	SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
	SQLINTEGER nativeError;
	SQLSMALLINT messageLength;

	SQLGetDiagRecA(handleType, handle, 1, sqlState, &nativeError, message,
					   SQL_MAX_MESSAGE_LENGTH, &messageLength);

	mLastErrorCode = static_cast<int>(nativeError);
	return std::string(reinterpret_cast<char *>(message));
}

// =============================================================================
// English: ODBCStatement Implementation
// 한글: ODBCStatement 구현
// =============================================================================

ODBCStatement::ODBCStatement(SQLHDBC conn)
	: mStatement(SQL_NULL_HANDLE), mConnection(conn), mPrepared(false),
		  mTimeout(30)
{
	SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, mConnection, &mStatement);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		throw DatabaseException("Failed to allocate ODBC statement handle");
	}
}

ODBCStatement::~ODBCStatement() { Close(); }

void ODBCStatement::SetQuery(const std::string &query)
{
	mQuery = query;
	mPrepared = false;
}

void ODBCStatement::SetTimeout(int seconds)
{
	mTimeout = seconds;
	SQLRETURN ret = SQLSetStmtAttr(mStatement, SQL_ATTR_QUERY_TIMEOUT,
									   (SQLPOINTER)(intptr_t)mTimeout, 0);
	CheckSQLReturn(ret, "Set timeout");
}

void ODBCStatement::BindParameter(size_t index, const std::string &value)
{
	if (mParams.size() < index) mParams.resize(index);
	auto &p    = mParams[index - 1];
	p.type     = ParamValue::Type::Text;
	p.text     = value;
	p.indicator = SQL_NTS;
}

void ODBCStatement::BindParameter(size_t index, int value)
{
	if (mParams.size() < index) mParams.resize(index);
	auto &p    = mParams[index - 1];
	p.type     = ParamValue::Type::Int;
	p.intVal   = value;
	p.indicator = 0;
}

void ODBCStatement::BindParameter(size_t index, long long value)
{
	if (mParams.size() < index) mParams.resize(index);
	auto &p     = mParams[index - 1];
	p.type      = ParamValue::Type::Int64;
	p.int64Val  = value;
	p.indicator = 0;
}

void ODBCStatement::BindParameter(size_t index, double value)
{
	if (mParams.size() < index) mParams.resize(index);
	auto &p      = mParams[index - 1];
	p.type       = ParamValue::Type::Double;
	p.doubleVal  = value;
	p.indicator  = 0;
}

void ODBCStatement::BindParameter(size_t index, bool value)
{
	if (mParams.size() < index) mParams.resize(index);
	auto &p    = mParams[index - 1];
	p.type     = ParamValue::Type::Bool;
	p.intVal   = value ? 1 : 0;
	p.indicator = 0;
}

void ODBCStatement::BindNullParameter(size_t index)
{
	if (mParams.size() < index) mParams.resize(index);
	auto &p    = mParams[index - 1];
	p.type     = ParamValue::Type::Null;
	p.indicator = SQL_NULL_DATA;
}

void ODBCStatement::BindParameters()
{
	// English: Bind each parameter with its native C type so that int/long/double
	//          are not silently converted through string (which truncates precision).
	//          mParams must not be modified between this call and SQLExecDirectA().
	// 한글: 각 파라미터를 네이티브 C 타입으로 바인딩해 int/long/double이
	//       문자열 경유 변환(정밀도 손실)되지 않도록 함.
	//       이 함수 호출과 SQLExecDirectA() 사이에 mParams를 수정해서는 안 됨.
	for (size_t i = 0; i < mParams.size(); ++i)
	{
		auto &p = mParams[i];
		const SQLUSMALLINT col = static_cast<SQLUSMALLINT>(i + 1);
		SQLRETURN ret;

		switch (p.type)
		{
		case ParamValue::Type::Text:
			p.indicator = SQL_NTS;
			ret = SQLBindParameter(
				mStatement, col, SQL_PARAM_INPUT,
				SQL_C_CHAR, SQL_VARCHAR,
				static_cast<SQLULEN>(p.text.length()), 0,
				(SQLPOINTER)p.text.c_str(),
				static_cast<SQLLEN>(p.text.length() + 1),
				&p.indicator);
			break;

		case ParamValue::Type::Int:
		case ParamValue::Type::Bool: // English: bool stored as 0/1 int
			p.indicator = 0;
			ret = SQLBindParameter(
				mStatement, col, SQL_PARAM_INPUT,
				SQL_C_LONG, SQL_INTEGER,
				0, 0, &p.intVal, 0, &p.indicator);
			break;

		case ParamValue::Type::Int64:
			p.indicator = 0;
			ret = SQLBindParameter(
				mStatement, col, SQL_PARAM_INPUT,
				SQL_C_SBIGINT, SQL_BIGINT,
				0, 0, &p.int64Val, 0, &p.indicator);
			break;

		case ParamValue::Type::Double:
			p.indicator = 0;
			ret = SQLBindParameter(
				mStatement, col, SQL_PARAM_INPUT,
				SQL_C_DOUBLE, SQL_DOUBLE,
				0, 0, &p.doubleVal, 0, &p.indicator);
			break;

		case ParamValue::Type::Null:
		default:
			p.indicator = SQL_NULL_DATA;
			ret = SQLBindParameter(
				mStatement, col, SQL_PARAM_INPUT,
				SQL_C_CHAR, SQL_VARCHAR,
				0, 0, nullptr, 0, &p.indicator);
			break;
		}

		CheckSQLReturn(ret, "Bind parameter");
	}
}

std::unique_ptr<IResultSet> ODBCStatement::ExecuteQuery()
{
	if (!mPrepared)
	{
		BindParameters();
		SQLRETURN ret =
			SQLExecDirectA(mStatement, (SQLCHAR *)mQuery.c_str(), SQL_NTS);
		CheckSQLReturn(ret, "Execute query");
		mPrepared = true;
	}
	return std::make_unique<ODBCResultSet>(mStatement);
}

int ODBCStatement::ExecuteUpdate()
{
	if (!mPrepared)
	{
		BindParameters();
		SQLRETURN ret =
			SQLExecDirectA(mStatement, (SQLCHAR *)mQuery.c_str(), SQL_NTS);
		CheckSQLReturn(ret, "Execute update");
		mPrepared = true;
	}

	SQLLEN rowCount = 0;
	SQLRETURN ret = SQLRowCount(mStatement, &rowCount);
	CheckSQLReturn(ret, "Get row count");
	return static_cast<int>(rowCount);
}

bool ODBCStatement::Execute()
{
	if (!mPrepared)
	{
		BindParameters();
		SQLRETURN ret =
			SQLExecDirectA(mStatement, (SQLCHAR *)mQuery.c_str(), SQL_NTS);
		if (ret == SQL_NO_DATA)
		{
			return false;
		}
		CheckSQLReturn(ret, "Execute");
		mPrepared = true;
		return true;
	}
	return true;
}

void ODBCStatement::AddBatch()
{
	// English: Snapshot current parameters as one batch entry, then reset for next item.
	//          The query string is shared across all batch items.
	// 한글: 현재 파라미터를 배치 항목으로 저장 후 다음 항목을 위해 초기화.
	//       쿼리 문자열은 모든 배치 항목이 공유.
	if (!mQuery.empty())
	{
		mBatches.push_back(BatchEntry{mParams});
		mParams.clear();
		mPrepared = false;
	}
}

std::vector<int> ODBCStatement::ExecuteBatch()
{
	// English: Execute each batch entry sequentially via SQLExecDirectA, collect row counts
	// 한글: SQLExecDirectA로 각 배치 항목을 순서대로 실행하고 행 수 수집
	std::vector<int> results;
	results.reserve(mBatches.size());

	for (auto &entry : mBatches)
	{
		mParams    = entry.params;
		mPrepared  = false;

		BindParameters();
		SQLRETURN ret =
			SQLExecDirectA(mStatement, (SQLCHAR *)mQuery.c_str(), SQL_NTS);

		if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
		{
			SQLLEN rowCount = 0;
			SQLRowCount(mStatement, &rowCount);
			results.push_back(static_cast<int>(rowCount));
		}
		else
		{
			results.push_back(-1);
		}

		// English: Close cursor / reset statement state for the next batch item
		// 한글: 다음 배치 항목을 위해 커서/구문 상태 초기화
		SQLFreeStmt(mStatement, SQL_CLOSE);
	}

	mBatches.clear();
	mParams.clear();
	mPrepared = false;
	return results;
}

void ODBCStatement::ClearParameters()
{
	mParams.clear();
	mPrepared = false;
}

void ODBCStatement::Close()
{
	if (mStatement != SQL_NULL_HANDLE)
	{
		SQLFreeHandle(SQL_HANDLE_STMT, mStatement);
		mStatement = SQL_NULL_HANDLE;
	}
}

void ODBCStatement::CheckSQLReturn(SQLRETURN ret, const std::string &operation)
{
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		throw DatabaseException(
			operation + ": " + GetSQLErrorMessage(mStatement, SQL_HANDLE_STMT));
	}
}

std::string ODBCStatement::GetSQLErrorMessage(SQLHANDLE handle,
												  SQLSMALLINT handleType)
{
	SQLCHAR sqlState[6];
	SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
	SQLINTEGER nativeError;
	SQLSMALLINT messageLength;

	SQLGetDiagRecA(handleType, handle, 1, sqlState, &nativeError, message,
					   SQL_MAX_MESSAGE_LENGTH, &messageLength);

	return std::string(reinterpret_cast<char *>(message));
}

// =============================================================================
// English: ODBCResultSet Implementation
// 한글: ODBCResultSet 구현
// =============================================================================

ODBCResultSet::ODBCResultSet(SQLHSTMT stmt)
	: mStatement(stmt), mHasData(false), mMetadataLoaded(false)
{
	LoadMetadata();
}

ODBCResultSet::~ODBCResultSet() { Close(); }

void ODBCResultSet::LoadMetadata()
{
	if (mMetadataLoaded)
		return;

	SQLSMALLINT columnCount = 0;
	SQLRETURN ret = SQLNumResultCols(mStatement, &columnCount);
	CheckSQLReturn(ret, "Get column count");

	mColumnNames.resize(columnCount);
	mColumnTypes.resize(columnCount);
	mColumnSizes.resize(columnCount);

	for (SQLSMALLINT i = 0; i < columnCount; ++i)
	{
		SQLCHAR columnName[256];
		SQLSMALLINT nameLength;
		SQLSMALLINT dataType;
		SQLULEN columnSize;
		SQLSMALLINT decimalDigits;
		SQLSMALLINT nullable;

		ret =
			SQLDescribeColA(mStatement, (SQLUSMALLINT)(i + 1), columnName,
							(SQLSMALLINT)sizeof(columnName), &nameLength,
							&dataType, &columnSize, &decimalDigits, &nullable);
		CheckSQLReturn(ret, "Describe column");

		mColumnNames[i] = std::string(reinterpret_cast<char *>(columnName));
		mColumnTypes[i] = dataType;
		mColumnSizes[i] = columnSize;
	}

	mMetadataLoaded = true;
	mRowCache.assign(static_cast<size_t>(columnCount), ColumnData{});
}

bool ODBCResultSet::Next()
{
	SQLRETURN ret = SQLFetch(mStatement);
	if (ret == SQL_NO_DATA)
	{
		mHasData = false;
		return false;
	}
	CheckSQLReturn(ret, "Fetch row");
	mHasData = true;
	// English: Invalidate column cache for the new row.
	// 한글: 새 행에 대한 컬럼 캐시 무효화.
	std::fill(mRowCache.begin(), mRowCache.end(), ColumnData{});
	return true;
}

const ODBCResultSet::ColumnData &ODBCResultSet::FetchColumn(size_t columnIndex)
{
	auto &col = mRowCache[columnIndex];
	if (col.fetched)
	{
		return col;
	}

	col.fetched  = true;
	SQLLEN indicator = 0;
	char buffer[4096] = {0};

	// English: Single SQLGetData call per column per row. Caching the result means
	//          IsNull() and GetString() can both be called without consuming the
	//          column cursor twice on forward-only result sets.
	// 한글: 행당 컬럼당 SQLGetData를 1회만 호출. 결과를 캐시하므로 IsNull()과
	//       GetString()을 각각 호출해도 forward-only 커서를 두 번 소비하지 않음.
	SQLRETURN ret = SQLGetData(
		mStatement, static_cast<SQLUSMALLINT>(columnIndex + 1),
		SQL_C_CHAR, buffer, static_cast<SQLLEN>(sizeof(buffer)), &indicator);

	if (indicator == SQL_NULL_DATA)
	{
		col.isNull = true;
		return col;
	}

	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		throw DatabaseException(
			"FetchColumn: " + GetSQLErrorMessage(mStatement, SQL_HANDLE_STMT));
	}

	col.isNull = false;
	// English: If indicator > buffer capacity the value is truncated to 4095 chars.
	//          Extend buffer or use streaming SQLGetData loop if longer values are needed.
	// 한글: indicator > 버퍼 크기이면 4095자로 잘림.
	//       더 긴 값이 필요하면 버퍼를 늘리거나 SQLGetData 스트리밍 루프를 사용.
	col.value = (indicator > static_cast<SQLLEN>(sizeof(buffer) - 1))
	                ? std::string(buffer, sizeof(buffer) - 1)
	                : std::string(buffer);
	return col;
}

bool ODBCResultSet::IsNull(size_t columnIndex)
{
	if (!mHasData)
	{
		throw DatabaseException("No current row");
	}
	return FetchColumn(columnIndex).isNull;
}

bool ODBCResultSet::IsNull(const std::string &columnName)
{
	size_t index = FindColumn(columnName);
	return IsNull(index);
}

std::string ODBCResultSet::GetString(size_t columnIndex)
{
	if (!mHasData)
	{
		throw DatabaseException("No current row");
	}
	const auto &col = FetchColumn(columnIndex);
	return col.isNull ? "" : col.value;
}

std::string ODBCResultSet::GetString(const std::string &columnName)
{
	size_t index = FindColumn(columnName);
	return GetString(index);
}

int ODBCResultSet::GetInt(size_t columnIndex)
{
	std::string value = GetString(columnIndex);
	try
	{
		return std::stoi(value);
	}
	catch (const std::exception &)
	{
		return 0;
	}
}

int ODBCResultSet::GetInt(const std::string &columnName)
{
	size_t index = FindColumn(columnName);
	return GetInt(index);
}

long long ODBCResultSet::GetLong(size_t columnIndex)
{
	std::string value = GetString(columnIndex);
	try
	{
		return std::stoll(value);
	}
	catch (const std::exception &)
	{
		return 0;
	}
}

long long ODBCResultSet::GetLong(const std::string &columnName)
{
	size_t index = FindColumn(columnName);
	return GetLong(index);
}

double ODBCResultSet::GetDouble(size_t columnIndex)
{
	std::string value = GetString(columnIndex);
	try
	{
		return std::stod(value);
	}
	catch (const std::exception &)
	{
		return 0.0;
	}
}

double ODBCResultSet::GetDouble(const std::string &columnName)
{
	size_t index = FindColumn(columnName);
	return GetDouble(index);
}

bool ODBCResultSet::GetBool(size_t columnIndex)
{
	int value = GetInt(columnIndex);
	return value != 0;
}

bool ODBCResultSet::GetBool(const std::string &columnName)
{
	size_t index = FindColumn(columnName);
	return GetBool(index);
}

size_t ODBCResultSet::GetColumnCount() const { return mColumnNames.size(); }

std::string ODBCResultSet::GetColumnName(size_t columnIndex) const
{
	if (columnIndex >= mColumnNames.size())
	{
		throw DatabaseException("Column index out of range");
	}
	return mColumnNames[columnIndex];
}

size_t ODBCResultSet::FindColumn(const std::string &columnName) const
{
	// English: Case-insensitive comparison — ODBC drivers vary (SQL Server uppercases,
	//          PostgreSQL lowercases). Matching on tolower prevents spurious not-found errors.
	// 한글: 대소문자 무시 비교 — ODBC 드라이버마다 대소문자가 다름 (SQL Server 대문자,
	//       PostgreSQL 소문자). tolower로 통일해 not-found 오류를 방지.
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
	throw DatabaseException("Column not found: " + columnName);
}

void ODBCResultSet::Close()
{
	// English: Statement handle is managed by the parent statement
	// 한글: Statement 핸들은 부모 statement에서 관리됨
}

void ODBCResultSet::CheckSQLReturn(SQLRETURN ret, const std::string &operation)
{
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		throw DatabaseException(
			operation + ": " + GetSQLErrorMessage(mStatement, SQL_HANDLE_STMT));
	}
}

std::string ODBCResultSet::GetSQLErrorMessage(SQLHANDLE handle,
												  SQLSMALLINT handleType)
{
	SQLCHAR sqlState[6];
	SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
	SQLINTEGER nativeError;
	SQLSMALLINT messageLength;

	SQLGetDiagRecA(handleType, handle, 1, sqlState, &nativeError, message,
					   SQL_MAX_MESSAGE_LENGTH, &messageLength);

	return std::string(reinterpret_cast<char *>(message));
}

} // namespace Database
} // namespace Network
