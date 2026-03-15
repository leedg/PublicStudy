// ODBCDatabase implementation

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
// ODBCDatabase Implementation
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
		SQLCHAR sqlState[6]       = {};
		SQLCHAR message[SQL_MAX_MESSAGE_LENGTH] = {};
		SQLINTEGER  nativeError   = 0;
		SQLSMALLINT messageLength = 0;

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
	// Open a temporary connection to validate the connection string,
	//          then discard it. Real connections are obtained via CreateConnection().
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
	// Open a dedicated connection for this statement.
	//          Ownership is transferred to ODBCStatement so the connection
	//          stays alive for the statement's entire lifetime.
	auto pConn = std::make_unique<ODBCConnection>(mEnvironment);
	pConn->Open(mConfig.mConnectionString);
	SQLHDBC connHandle = pConn->GetHandle();
	return std::make_unique<ODBCStatement>(connHandle, std::move(pConn));
}

void ODBCDatabase::BeginTransaction()
{
	// Transaction state is per-connection. Use IConnection::BeginTransaction()
	//          on a connection obtained from CreateConnection() or a ConnectionPool.
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
// ODBCConnection Implementation
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
		return; // Already connected
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
	SQLCHAR sqlState[6]      = {};
	SQLCHAR message[SQL_MAX_MESSAGE_LENGTH] = {};
	SQLINTEGER nativeError;
	SQLSMALLINT messageLength;

	SQLGetDiagRecA(handleType, handle, 1, sqlState, &nativeError, message,
					   SQL_MAX_MESSAGE_LENGTH, &messageLength);

	mLastErrorCode = static_cast<int>(nativeError);
	return std::string(reinterpret_cast<char *>(message));
}

// =============================================================================
// ODBCStatement Implementation
// =============================================================================

ODBCStatement::ODBCStatement(SQLHDBC conn, std::unique_ptr<ODBCConnection> ownerConn)
	: mOwnerConn(std::move(ownerConn)), mStatement(SQL_NULL_HANDLE),
	  mConnection(conn), mPrepared(false), mTimeout(30)
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
	SetParam<ParamValue::Type::Int, &ParamValue::intVal>(index, value);
}

void ODBCStatement::BindParameter(size_t index, long long value)
{
	SetParam<ParamValue::Type::Int64, &ParamValue::int64Val>(index, value);
}

void ODBCStatement::BindParameter(size_t index, double value)
{
	SetParam<ParamValue::Type::Double, &ParamValue::doubleVal>(index, value);
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
	// Bind each parameter with its native C type so that int/long/double
	//          are not silently converted through string (which truncates precision).
	//          mParams must not be modified between this call and SQLExecDirectA().
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
			p.indicator = 0;
			ret = SQLBindParameter(
				mStatement, col, SQL_PARAM_INPUT,
				SQL_C_LONG, SQL_INTEGER,
				0, 0, &p.intVal, 0, &p.indicator);
			break;

		case ParamValue::Type::Bool:
			// Use SQL_C_BIT/SQL_BIT so PostgreSQL BOOLEAN and SQL Server BIT
			//          both accept the value without implicit cast errors.
			//          intVal holds 0 or 1; on little-endian the first byte is the bit value.
			p.indicator = 0;
			ret = SQLBindParameter(
				mStatement, col, SQL_PARAM_INPUT,
				SQL_C_BIT, SQL_BIT,
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
	// Snapshot current parameters as one batch entry, then reset for next item.
	//          The query string is shared across all batch items.
	if (!mQuery.empty())
	{
		mBatches.push_back(BatchEntry{mParams});
		mParams.clear();
		mPrepared = false;
	}
}

std::vector<int> ODBCStatement::ExecuteBatch()
{
	// Execute each batch entry sequentially via SQLExecDirectA, collect row counts
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

		// Close cursor / reset statement state for the next batch item
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
	SQLCHAR sqlState[6]      = {};
	SQLCHAR message[SQL_MAX_MESSAGE_LENGTH] = {};
	SQLINTEGER nativeError;
	SQLSMALLINT messageLength;

	SQLGetDiagRecA(handleType, handle, 1, sqlState, &nativeError, message,
					   SQL_MAX_MESSAGE_LENGTH, &messageLength);

	return std::string(reinterpret_cast<char *>(message));
}

// =============================================================================
// ODBCResultSet Implementation
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
	// Invalidate column cache for the new row.
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

	// Single SQLGetData call per column per row. Caching the result means
	//          IsNull() and GetString() can both be called without consuming the
	//          column cursor twice on forward-only result sets.
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
	// If indicator > buffer capacity the value is truncated to 4095 chars.
	//          Extend buffer or use streaming SQLGetData loop if longer values are needed.
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

std::string ODBCResultSet::GetString(size_t columnIndex)
{
	if (!mHasData)
	{
		throw DatabaseException("No current row");
	}
	const auto &col = FetchColumn(columnIndex);
	return col.isNull ? "" : col.value;
}

int ODBCResultSet::GetInt(size_t columnIndex)
{
	return ParseAs<int>(GetString(columnIndex), 0);
}

long long ODBCResultSet::GetLong(size_t columnIndex)
{
	return ParseAs<long long>(GetString(columnIndex), 0LL);
}

double ODBCResultSet::GetDouble(size_t columnIndex)
{
	return ParseAs<double>(GetString(columnIndex), 0.0);
}

bool ODBCResultSet::GetBool(size_t columnIndex)
{
	return GetInt(columnIndex) != 0;
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
	// Case-insensitive comparison — ODBC drivers vary (SQL Server uppercases,
	//          PostgreSQL lowercases). Matching on tolower prevents spurious not-found errors.
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
	// Statement handle is managed by the parent statement
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
	SQLCHAR sqlState[6]      = {};
	SQLCHAR message[SQL_MAX_MESSAGE_LENGTH] = {};
	SQLINTEGER nativeError;
	SQLSMALLINT messageLength;

	SQLGetDiagRecA(handleType, handle, 1, sqlState, &nativeError, message,
					   SQL_MAX_MESSAGE_LENGTH, &messageLength);

	return std::string(reinterpret_cast<char *>(message));
}

} // namespace Database
} // namespace Network
