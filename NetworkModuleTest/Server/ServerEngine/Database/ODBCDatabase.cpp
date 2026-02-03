// English: ODBCDatabase implementation
// 한글: ODBCDatabase 구현

#include "ODBCDatabase.h"
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
		CleanupEnvironment();
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
	auto pConnection = CreateConnection();
	pConnection->Open(mConfig.mConnectionString);
	pConnection->BeginTransaction();
}

void ODBCDatabase::CommitTransaction()
{
	auto pConnection = CreateConnection();
	pConnection->Open(mConfig.mConnectionString);
	pConnection->CommitTransaction();
}

void ODBCDatabase::RollbackTransaction()
{
	auto pConnection = CreateConnection();
	pConnection->Open(mConfig.mConnectionString);
	pConnection->RollbackTransaction();
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

	SQLRETURN ret = SQLDriverConnectA(
		mConnection, nullptr, (SQLCHAR *)connectionString.c_str(), SQL_NTS,
		connStrOut, sizeof(connStrOut), &connStrOutLength, SQL_DRIVER_NOPROMPT);

	CheckSQLReturn(ret, "Connection");
	mConnected = true;
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
	size_t newSize = std::max(mParameters.size(), index);
	mParameters.resize(newSize);
	mParameters[index - 1] = value;
	mParameterLengths.resize(newSize);
	mParameterLengths[index - 1] = SQL_NTS;
}

void ODBCStatement::BindParameter(size_t index, int value)
{
	size_t newSize = std::max(mParameters.size(), index);
	mParameters.resize(newSize);
	mParameters[index - 1] = std::to_string(value);
	mParameterLengths.resize(newSize);
	mParameterLengths[index - 1] = 0;
}

void ODBCStatement::BindParameter(size_t index, long long value)
{
	size_t newSize = std::max(mParameters.size(), index);
	mParameters.resize(newSize);
	mParameters[index - 1] = std::to_string(value);
	mParameterLengths.resize(newSize);
	mParameterLengths[index - 1] = 0;
}

void ODBCStatement::BindParameter(size_t index, double value)
{
	size_t newSize = std::max(mParameters.size(), index);
	mParameters.resize(newSize);
	mParameters[index - 1] = std::to_string(value);
	mParameterLengths.resize(newSize);
	mParameterLengths[index - 1] = 0;
}

void ODBCStatement::BindParameter(size_t index, bool value)
{
	size_t newSize = std::max(mParameters.size(), index);
	mParameters.resize(newSize);
	mParameters[index - 1] = value ? "1" : "0";
	mParameterLengths.resize(newSize);
	mParameterLengths[index - 1] = 0;
}

void ODBCStatement::BindNullParameter(size_t index)
{
	size_t newSize = std::max(mParameters.size(), index);
	mParameters.resize(newSize);
	mParameters[index - 1] = "";
	mParameterLengths.resize(newSize);
	mParameterLengths[index - 1] = SQL_NULL_DATA;
}

void ODBCStatement::BindParameters()
{
	for (size_t i = 0; i < mParameters.size(); ++i)
	{
		SQLRETURN ret = SQLBindParameter(
			mStatement, (SQLUSMALLINT)(i + 1), SQL_PARAM_INPUT, SQL_C_CHAR,
			SQL_VARCHAR, 0, 0, (SQLPOINTER)mParameters[i].c_str(),
			(SQLLEN)mParameters[i].length(), &mParameterLengths[i]);
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
	// English: ODBC batch implementation - simplified
	// 한글: ODBC 배치 구현 - 단순화
}

std::vector<int> ODBCStatement::ExecuteBatch()
{
	// English: ODBC batch execution implementation - simplified
	// 한글: ODBC 배치 실행 구현 - 단순화
	return std::vector<int>();
}

void ODBCStatement::ClearParameters()
{
	mParameters.clear();
	mParameterLengths.clear();
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
	return true;
}

bool ODBCResultSet::IsNull(size_t columnIndex)
{
	SQLLEN indicator;
	SQLCHAR buffer[1];
	SQLRETURN ret =
		SQLGetData(mStatement, static_cast<SQLUSMALLINT>(columnIndex + 1),
					   SQL_C_CHAR, buffer, 0, &indicator);
	CheckSQLReturn(ret, "Get data (null check)");
	return indicator == SQL_NULL_DATA;
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

	std::string value;
	SQLLEN indicator = 0;
	char buffer[4096] = {0};

	SQLRETURN ret =
		SQLGetData(mStatement, static_cast<SQLUSMALLINT>(columnIndex + 1),
					   SQL_C_CHAR, buffer, sizeof(buffer), &indicator);

	if (indicator == SQL_NULL_DATA)
	{
		return "";
	}

	CheckSQLReturn(ret, "Get data (string)");

	if (ret == SQL_SUCCESS_WITH_INFO &&
		indicator > static_cast<SQLLEN>(sizeof(buffer) - 1))
	{
		// English: Handle long data - simplified for this example
		// 한글: 긴 데이터 처리 - 예제용 단순화
		value = std::string(buffer, sizeof(buffer) - 1);
	}
	else
	{
		value = std::string(buffer);
	}

	return value;
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
	for (size_t i = 0; i < mColumnNames.size(); ++i)
	{
		if (mColumnNames[i] == columnName)
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
