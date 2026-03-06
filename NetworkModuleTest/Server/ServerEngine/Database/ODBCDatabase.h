#pragma once

// English: ODBC implementation of database interfaces
// ?쒓?: ?곗씠?곕쿋?댁뒪 ?명꽣?섏씠?ㅼ쓽 ODBC 援ы쁽

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IConnection.h"
#include "../Interfaces/IDatabase.h"
#include "../Interfaces/IResultSet.h"
#include "../Interfaces/IStatement.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <memory>
// ?쒓?: ODBC ?ㅻ뜑媛 ?꾩슂濡??섎뒗 Windows ??낆쓣 癒쇱? ?뺤쓽?쒕떎.
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <vector>

namespace Network
{
namespace Database
{

// English: Forward declarations
// ?쒓?: ?꾨갑 ?좎뼵
class ODBCConnection;
class ODBCStatement;
class ODBCResultSet;

// =============================================================================
// English: ODBCDatabase class
// ?쒓?: ODBCDatabase ?대옒??
// =============================================================================

/**
 * English: ODBC implementation of IDatabase
 * ?쒓?: IDatabase??ODBC 援ы쁽
 */
class ODBCDatabase : public IDatabase
{
  public:
	ODBCDatabase();
	virtual ~ODBCDatabase();

	// English: IDatabase interface
	// ?쒓?: IDatabase ?명꽣?섏씠??
	void Connect(const DatabaseConfig &config) override;
	void Disconnect() override;
	bool IsConnected() const override;

	std::unique_ptr<IConnection> CreateConnection() override;
	std::unique_ptr<IStatement> CreateStatement() override;

	void BeginTransaction() override;
	void CommitTransaction() override;
	void RollbackTransaction() override;

	DatabaseType GetType() const override { return DatabaseType::ODBC; }

	const DatabaseConfig &GetConfig() const override { return mConfig; }

	SQLHENV GetEnvironment() const { return mEnvironment; }

  private:
	// English: Helper methods
	// ?쒓?: ?ы띁 硫붿꽌??
	void InitializeEnvironment();
	void CleanupEnvironment();
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation,
						SQLHANDLE handle, SQLSMALLINT handleType);

  private:
	DatabaseConfig mConfig;
	SQLHENV mEnvironment;
	bool mConnected;
	std::unique_ptr<ODBCConnection> mSharedConnection;
};

// =============================================================================
// English: ODBCConnection class
// ?쒓?: ODBCConnection ?대옒??
// =============================================================================

/**
 * English: ODBC implementation of IConnection
 * ?쒓?: IConnection??ODBC 援ы쁽
 */
class ODBCConnection : public IConnection
{
  public:
	explicit ODBCConnection(SQLHENV env);
	virtual ~ODBCConnection();

	// English: IConnection interface
	// ?쒓?: IConnection ?명꽣?섏씠??
	void Open(const std::string &connectionString) override;
	void Close() override;
	bool IsOpen() const override;

	std::unique_ptr<IStatement> CreateStatement() override;
	void BeginTransaction() override;
	void CommitTransaction() override;
	void RollbackTransaction() override;

	int GetLastErrorCode() const override { return mLastErrorCode; }

	std::string GetLastError() const override { return mLastError; }

	SQLHDBC GetHandle() const { return mConnection; }

  private:
	// English: Helper methods
	// ?쒓?: ?ы띁 硫붿꽌??
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string GetSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);

  private:
	SQLHDBC mConnection;
	SQLHENV mEnvironment;
	bool mConnected;
	std::string mLastError;
	int mLastErrorCode;
};

// =============================================================================
// English: ODBCStatement class
// ?쒓?: ODBCStatement ?대옒??
// =============================================================================

/**
 * English: ODBC implementation of IStatement
 * ?쒓?: IStatement??ODBC 援ы쁽
 */
class ODBCStatement : public IStatement
{
  public:
	explicit ODBCStatement(SQLHDBC conn);
	virtual ~ODBCStatement();

	// English: IStatement interface
	// ?쒓?: IStatement ?명꽣?섏씠??
	void SetQuery(const std::string &query) override;
	void SetTimeout(int seconds) override;

	void BindParameter(size_t index, const std::string &value) override;
	void BindParameter(size_t index, int value) override;
	void BindParameter(size_t index, long long value) override;
	void BindParameter(size_t index, double value) override;
	void BindParameter(size_t index, bool value) override;
	void BindNullParameter(size_t index) override;

	std::unique_ptr<IResultSet> ExecuteQuery() override;
	int ExecuteUpdate() override;
	bool Execute() override;

	void AddBatch() override;
	std::vector<int> ExecuteBatch() override;

	void ClearParameters() override;
	void Close() override;

  private:
	// English: Helper methods
	// ?쒓?: ?ы띁 硫붿꽌??
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string GetSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);
	void BindParameters();

  private:
	// English: Batch entry ??snapshot of parameters for one batch item
	// ?쒓?: 諛곗튂 ??ぉ ??諛곗튂 ?꾩씠???섎굹???뚮씪誘명꽣 ?ㅻ깄??
	struct BatchEntry
	{
		std::vector<std::string> parameters;
		std::vector<SQLLEN> parameterLengths;
	};

  private:
	SQLHSTMT mStatement;
	SQLHDBC mConnection;
	std::string mQuery;
	std::vector<std::string> mParameters;
	std::vector<SQLLEN> mParameterLengths;
	std::vector<BatchEntry> mBatches;
	bool mPrepared;
	int mTimeout;
};

// =============================================================================
// English: ODBCResultSet class
// ?쒓?: ODBCResultSet ?대옒??
// =============================================================================

/**
 * English: ODBC implementation of IResultSet
 * ?쒓?: IResultSet??ODBC 援ы쁽
 */
class ODBCResultSet : public IResultSet
{
  public:
	explicit ODBCResultSet(SQLHSTMT stmt);
	virtual ~ODBCResultSet();

	// English: IResultSet interface
	// ?쒓?: IResultSet ?명꽣?섏씠??
	bool Next() override;
	bool IsNull(size_t columnIndex) override;
	bool IsNull(const std::string &columnName) override;

	std::string GetString(size_t columnIndex) override;
	std::string GetString(const std::string &columnName) override;

	int GetInt(size_t columnIndex) override;
	int GetInt(const std::string &columnName) override;

	long long GetLong(size_t columnIndex) override;
	long long GetLong(const std::string &columnName) override;

	double GetDouble(size_t columnIndex) override;
	double GetDouble(const std::string &columnName) override;

	bool GetBool(size_t columnIndex) override;
	bool GetBool(const std::string &columnName) override;

	size_t GetColumnCount() const override;
	std::string GetColumnName(size_t columnIndex) const override;
	size_t FindColumn(const std::string &columnName) const override;

	void Close() override;

  private:
	// English: Helper methods
	// ?쒓?: ?ы띁 硫붿꽌??
	void LoadMetadata();
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string GetSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);

  private:
	SQLHSTMT mStatement;
	bool mHasData;
	std::vector<std::string> mColumnNames;
	std::vector<SQLSMALLINT> mColumnTypes;
	std::vector<SQLULEN> mColumnSizes;
	bool mMetadataLoaded;
};

} // namespace Database
} // namespace Network
