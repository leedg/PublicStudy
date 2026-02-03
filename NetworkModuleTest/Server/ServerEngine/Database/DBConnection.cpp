// English: DBConnection implementation
// ???: DBConnection ?닌뗭겱

#include "DBConnection.h"
#include <iostream>

namespace Network::Database
{

DBConnection::DBConnection()
	: mConnected(false)
#ifdef _WIN32
		  ,
		  mEnv(SQL_NULL_HENV), mDbc(SQL_NULL_HDBC), mStmt(SQL_NULL_HSTMT)
#endif
{
}

DBConnection::~DBConnection() { Disconnect(); }

bool DBConnection::Connect(const std::string &connectionString)
{
#ifdef _WIN32
	SQLRETURN ret;

	// English: Allocate environment handle
	// ???: ??띻펾 ?紐껊굶 ?醫딅뼣
	ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &mEnv);
	if (!SQL_SUCCEEDED(ret))
	{
		mLastError = "Failed to allocate environment handle";
		std::cerr << "[ERROR] " << mLastError << std::endl;
		return false;
	}

	// English: Set ODBC version
	// ???: ODBC 甕곌쑴????쇱젟
	ret = SQLSetEnvAttr(mEnv, SQL_ATTR_ODBC_VERSION,
						reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
	if (!SQL_SUCCEEDED(ret))
	{
		mLastError = "Failed to set ODBC version";
		std::cerr << "[ERROR] " << mLastError << std::endl;
		return false;
	}

	// English: Allocate connection handle
	// ???: ?怨뚭퍙 ?紐껊굶 ?醫딅뼣
	ret = SQLAllocHandle(SQL_HANDLE_DBC, mEnv, &mDbc);
	if (!SQL_SUCCEEDED(ret))
	{
		mLastError = "Failed to allocate connection handle";
		std::cerr << "[ERROR] " << mLastError << std::endl;
		return false;
	}

	// English: Connect
	// ???: ?怨뚭퍙
	SQLCHAR outConnStr[1024];
	SQLSMALLINT outConnStrLen;

	ret = SQLDriverConnectA(mDbc, nullptr,
							reinterpret_cast<SQLCHAR *>(
								const_cast<char *>(connectionString.c_str())),
							SQL_NTS, outConnStr, sizeof(outConnStr),
							&outConnStrLen, SQL_DRIVER_NOPROMPT);

	if (!SQL_SUCCEEDED(ret))
	{
		SQLCHAR sqlState[6];
		SQLINTEGER nativeError;
		SQLCHAR msgText[256];
		SQLSMALLINT msgLen;

		SQLGetDiagRecA(SQL_HANDLE_DBC, mDbc, 1, sqlState, &nativeError, msgText,
						   sizeof(msgText), &msgLen);

		mLastError = std::string("Connection failed: ") +
					 reinterpret_cast<char *>(msgText);
		std::cerr << "[ERROR] " << mLastError << std::endl;
		return false;
	}

	// English: Allocate statement handle
	// ???: ?닌됎??紐껊굶 ?醫딅뼣
	ret = SQLAllocHandle(SQL_HANDLE_STMT, mDbc, &mStmt);
	if (!SQL_SUCCEEDED(ret))
	{
		mLastError = "Failed to allocate statement handle";
		std::cerr << "[ERROR] " << mLastError << std::endl;
		return false;
	}

	mConnected = true;
	std::cout << "[INFO] Database connected" << std::endl;
	return true;
#else
	mLastError = "Database not supported on this platform";
	return false;
#endif
}

void DBConnection::Disconnect()
{
#ifdef _WIN32
	if (mStmt != SQL_NULL_HSTMT)
	{
		SQLFreeHandle(SQL_HANDLE_STMT, mStmt);
		mStmt = SQL_NULL_HSTMT;
	}

	if (mDbc != SQL_NULL_HDBC)
	{
		SQLDisconnect(mDbc);
		SQLFreeHandle(SQL_HANDLE_DBC, mDbc);
		mDbc = SQL_NULL_HDBC;
	}

	if (mEnv != SQL_NULL_HENV)
	{
		SQLFreeHandle(SQL_HANDLE_ENV, mEnv);
		mEnv = SQL_NULL_HENV;
	}
#endif

	mConnected = false;
}

bool DBConnection::Execute(const std::string &query)
{
#ifdef _WIN32
	if (!mConnected)
	{
		mLastError = "Not connected to database";
		return false;
	}

	// English: Close any existing cursor before executing new statement
	// ???: ???닌됎???쎈뻬 ??疫꿸퀣???뚣끉苑???る┛
	SQLCloseCursor(mStmt);

	SQLRETURN ret = SQLExecDirectA(
		mStmt, reinterpret_cast<SQLCHAR *>(const_cast<char *>(query.c_str())),
		SQL_NTS);

	if (!SQL_SUCCEEDED(ret))
	{
		SQLCHAR sqlState[6];
		SQLINTEGER nativeError;
		SQLCHAR msgText[256];
		SQLSMALLINT msgLen;

		SQLGetDiagRecA(SQL_HANDLE_STMT, mStmt, 1, sqlState, &nativeError,
						   msgText, sizeof(msgText), &msgLen);

		mLastError =
			std::string("Query failed: ") + reinterpret_cast<char *>(msgText);
		std::cerr << "[ERROR] " << mLastError << std::endl;
		return false;
	}

	return true;
#else
	mLastError = "Database not supported on this platform";
	return false;
#endif
}

} // namespace Network::Database
