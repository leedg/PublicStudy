#pragma once

// ODBC implementation of database interfaces

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
#include <atomic>
#include <memory>
#include <type_traits>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <vector>

namespace Network
{
namespace Database
{

// Forward declarations
class ODBCConnection;
class ODBCStatement;
class ODBCResultSet;

// =============================================================================
// ODBCDatabase class
// =============================================================================

/**
 * ODBC implementation of IDatabase
 */
class ODBCDatabase : public IDatabase
{
  public:
	ODBCDatabase();
	virtual ~ODBCDatabase();

	// IDatabase interface
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
	// Helper methods
	void InitializeEnvironment();
	void CleanupEnvironment();
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation,
						SQLHANDLE handle, SQLSMALLINT handleType);

  private:
	DatabaseConfig mConfig;
	SQLHENV mEnvironment;
	std::atomic<bool> mConnected;
};

// =============================================================================
// ODBCConnection class
// =============================================================================

/**
 * ODBC implementation of IConnection
 */
class ODBCConnection : public IConnection
{
  public:
	explicit ODBCConnection(SQLHENV env);
	virtual ~ODBCConnection();

	// IConnection interface
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
	// Helper methods
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string GetSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);

  private:
	SQLHDBC mConnection;
	SQLHENV mEnvironment;
	std::atomic<bool> mConnected;
	std::string mLastError;
	int mLastErrorCode;
};

// =============================================================================
// ODBCStatement class
// =============================================================================

/**
 * ODBC implementation of IStatement
 */
class ODBCStatement : public IStatement
{
  public:
	// conn must remain alive for the lifetime of this statement.
	//          ownerConn (optional) transfers ownership when a statement is created
	//          via IDatabase::CreateStatement() — keeps the per-statement connection alive.
	explicit ODBCStatement(SQLHDBC conn,
	                       std::unique_ptr<ODBCConnection> ownerConn = nullptr);
	virtual ~ODBCStatement();

	// IStatement interface
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
	// Helper methods
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string GetSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);
	void BindParameters();

  private:
	// Typed parameter value — stores the native C value for each bind type.
	//          SQLBindParameter receives a pointer into this struct; mParams must not
	//          be modified between BindParameters() and SQLExecDirectA().
	struct ParamValue
	{
		enum class Type { Text, Int, Int64, Double, Bool, Null } type = Type::Null;
		std::string text;
		int         intVal    = 0;
		long long   int64Val  = 0;
		double      doubleVal = 0.0;
		SQLLEN      indicator = SQL_NULL_DATA;
	};

	// Batch entry — snapshot of parameters for one batch item
	struct BatchEntry
	{
		std::vector<ParamValue> params;
	};

	// Resize mParams and assign a fixed-size typed slot (int / long long / double).
	//          TypeTag is the ParamValue::Type enum; FieldPtr is a member pointer to the value field.
	template<ParamValue::Type TypeTag, auto FieldPtr>
	void SetParam(size_t index, decltype(ParamValue{}.*FieldPtr) value)
	{
		if (mParams.size() < index) mParams.resize(index);
		auto &p     = mParams[index - 1];
		p.type      = TypeTag;
		p.*FieldPtr = value;
		p.indicator = 0;
	}

  private:
	// Keeps the per-statement connection alive when created via IDatabase::CreateStatement().
	std::unique_ptr<ODBCConnection> mOwnerConn;
	SQLHSTMT mStatement;
	SQLHDBC mConnection;
	std::string mQuery;
	std::vector<ParamValue> mParams;
	std::vector<BatchEntry> mBatches;
	bool mPrepared;
	int mTimeout;
};

// =============================================================================
// ODBCResultSet class
// =============================================================================

/**
 * ODBC implementation of IResultSet
 */
class ODBCResultSet : public IResultSet
{
  public:
	explicit ODBCResultSet(SQLHSTMT stmt);
	virtual ~ODBCResultSet();

	// IResultSet interface (name overloads inherited from IResultSet default impl)
	bool Next() override;
	bool IsNull(size_t columnIndex) override;
	std::string GetString(size_t columnIndex) override;
	int GetInt(size_t columnIndex) override;
	long long GetLong(size_t columnIndex) override;
	double GetDouble(size_t columnIndex) override;
	bool GetBool(size_t columnIndex) override;
	size_t GetColumnCount() const override;
	std::string GetColumnName(size_t columnIndex) const override;
	size_t FindColumn(const std::string &columnName) const override;
	void Close() override;

  private:
	// Per-row column data cache. FetchColumn() populates a slot on first access
	//          and returns cached data on subsequent calls within the same row.
	//          This prevents SQLGetData from being called twice on the same column
	//          (which advances the stream cursor on forward-only result sets).
	//          The cache is invalidated on each Next() call.
	struct ColumnData
	{
		bool fetched = false;
		bool isNull  = false;
		std::string value;
	};

	// Helper methods
	void LoadMetadata();
	const ColumnData &FetchColumn(size_t columnIndex);
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string GetSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);

	// Parse a string column value as a numeric type; returns defaultVal on failure.
	template<typename T>
	static T ParseAs(const std::string &s, T defaultVal) noexcept
	{
		try {
			if constexpr (std::is_same_v<T, int>)       return std::stoi(s);
			if constexpr (std::is_same_v<T, long long>) return std::stoll(s);
			if constexpr (std::is_same_v<T, double>)    return std::stod(s);
		} catch (...) {}
		return defaultVal;
	}

  private:
	SQLHSTMT mStatement;
	bool mHasData;
	std::vector<std::string> mColumnNames;
	std::vector<SQLSMALLINT> mColumnTypes;
	std::vector<SQLULEN> mColumnSizes;
	bool mMetadataLoaded;
	std::vector<ColumnData> mRowCache;
};

} // namespace Database
} // namespace Network
