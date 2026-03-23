#pragma once

// PostgreSQL (libpq) implementation of database interfaces.
// Requires HAVE_LIBPQ to be defined (and libpq linked).
// No stub class is provided — if HAVE_LIBPQ is absent the types simply do not
// exist and DatabaseFactory::CreatePostgreSQLDatabase() will throw at runtime.

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IConnection.h"
#include "../Interfaces/IDatabase.h"
#include "../Interfaces/IResultSet.h"
#include "../Interfaces/IStatement.h"
#include <memory>
#include <string>
#include <vector>

#ifdef HAVE_LIBPQ
#include <libpq-fe.h>
#endif

namespace Network
{
namespace Database
{

#ifdef HAVE_LIBPQ

class PostgreSQLConnection;
class PostgreSQLStatement;
class PostgreSQLResultSet;

// =============================================================================
// PostgreSQLResultSet — wraps PGresult* with cursor-based row iteration
// =============================================================================

class PostgreSQLResultSet : public IResultSet
{
  public:
	// Takes ownership of result (calls PQclear on Close/destruction).
	explicit PostgreSQLResultSet(PGresult *result);
	~PostgreSQLResultSet() override;

	bool        Next() override;
	bool        IsNull(size_t columnIndex) override;
	std::string GetString(size_t columnIndex) override;
	int         GetInt(size_t columnIndex) override;
	long long   GetLong(size_t columnIndex) override;
	double      GetDouble(size_t columnIndex) override;
	bool        GetBool(size_t columnIndex) override;

	size_t      GetColumnCount() const override;
	std::string GetColumnName(size_t columnIndex) const override;
	size_t      FindColumn(const std::string &columnName) const override;

	void Close() override;

  private:
	void CheckRow() const;

	PGresult *           mResult;
	int                  mNumRows;
	int                  mCurrentRow; // -1 before first Next()
	std::vector<std::string> mColumnNames;
};

// =============================================================================
// PostgreSQLStatement — executes parameterised queries via PQexecParams.
// Queries must use PostgreSQL-style placeholders ($1, $2, ...).
// =============================================================================

class PostgreSQLStatement : public IStatement
{
  public:
	explicit PostgreSQLStatement(PGconn *conn);
	~PostgreSQLStatement() override;

	void SetQuery(const std::string &query) override;
	// Applies SET statement_timeout (session-level) before each execution.
	void SetTimeout(int seconds) override;

	void BindParameter(size_t index, const std::string &value) override;
	void BindParameter(size_t index, int value) override;
	void BindParameter(size_t index, long long value) override;
	void BindParameter(size_t index, double value) override;
	void BindParameter(size_t index, bool value) override;
	void BindNullParameter(size_t index) override;

	std::unique_ptr<IResultSet> ExecuteQuery() override;
	int                         ExecuteUpdate() override;
	bool                        Execute() override;

	void             AddBatch() override;
	std::vector<int> ExecuteBatch() override;

	void ClearParameters() override;
	void Close() override;

  private:
	struct Param
	{
		bool        isNull = true;
		std::string value;
	};

	void       EnsureQuery() const;
	void       SetParam(size_t index, std::string value);
	PGresult * RunExecParams(const std::vector<Param> &params);
	void       ApplyTimeout() const;
	static int ParseAffectedRows(PGresult *result);

	PGconn *    mConn;
	std::string mQuery;
	int         mTimeoutSeconds = 0;
	std::vector<Param>               mCurrentParams;
	std::vector<std::vector<Param>>  mBatchParams;
};

// =============================================================================
// PostgreSQLConnection — owns a PGconn* opened from a libpq connection string
// =============================================================================

class PostgreSQLConnection : public IConnection
{
  public:
	PostgreSQLConnection();
	~PostgreSQLConnection() override;

	// connectionString: libpq conninfo ("host=... dbname=...") or URI
	void Open(const std::string &connectionString) override;
	void Close() override;
	bool IsOpen() const override;

	std::unique_ptr<IStatement> CreateStatement() override;

	void BeginTransaction() override;
	void CommitTransaction() override;
	void RollbackTransaction() override;

	int         GetLastErrorCode() const override;
	std::string GetLastError() const override;

  private:
	void ExecRaw(const char *sql);

	PGconn *    mConn           = nullptr;
	bool        mInTransaction  = false;
	int         mLastErrorCode  = 0;
	std::string mLastError;
};

// =============================================================================
// PostgreSQLDatabase — manages a shared PGconn* for database-level operations.
// For per-operation isolation, prefer CreateConnection().
// =============================================================================

class PostgreSQLDatabase : public IDatabase
{
  public:
	PostgreSQLDatabase();
	~PostgreSQLDatabase() override;

	// config.mConnectionString: libpq conninfo string or URI
	void Connect(const DatabaseConfig &config) override;
	void Disconnect() override;
	bool IsConnected() const override;

	std::unique_ptr<IConnection> CreateConnection() override;
	std::unique_ptr<IStatement>  CreateStatement() override;

	void BeginTransaction() override;
	void CommitTransaction() override;
	void RollbackTransaction() override;

	DatabaseType           GetType()   const override { return DatabaseType::PostgreSQL; }
	const DatabaseConfig & GetConfig() const override { return mConfig; }

  private:
	void ExecRaw(const char *sql);

	DatabaseConfig mConfig;
	PGconn *       mConn      = nullptr;
	bool           mConnected = false;
};

#endif // HAVE_LIBPQ

} // namespace Database
} // namespace Network
