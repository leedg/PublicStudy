#pragma once

// SQLite implementation of database interfaces
//          Compile with HAVE_SQLITE3 defined (and link sqlite3) for full support.
//          Without HAVE_SQLITE3 a stub class that throws on Connect() is provided
//          so the rest of the build remains unchanged.

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IConnection.h"
#include "../Interfaces/IDatabase.h"
#include "../Interfaces/IResultSet.h"
#include "../Interfaces/IStatement.h"
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#ifdef HAVE_SQLITE3
#include <sqlite3.h>
#endif

namespace Network
{
namespace Database
{

#ifdef HAVE_SQLITE3

// Forward declarations
class SQLiteConnection;
class SQLiteStatement;
class SQLiteResultSet;

// =============================================================================
// SQLiteResultSet — wraps sqlite3_stmt result iteration
// =============================================================================

class SQLiteResultSet : public IResultSet
{
  public:
	// Takes ownership of the prepared statement
	explicit SQLiteResultSet(sqlite3_stmt *stmt);
	virtual ~SQLiteResultSet();

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
	void LoadColumnNames();
	int ResolveColumn(const std::string &columnName) const;

  private:
	sqlite3_stmt *mStmt;
	bool mDone;
	bool mHasData; // true only while a SQLITE_ROW has been returned by Next()
	std::vector<std::string> mColumnNames;
};

// =============================================================================
// SQLiteStatement — prepares and executes SQL against a sqlite3 handle
// =============================================================================

class SQLiteStatement : public IStatement
{
  public:
	explicit SQLiteStatement(sqlite3 *db);
	virtual ~SQLiteStatement();

	void SetQuery(const std::string &query) override;
	void SetTimeout([[maybe_unused]] int seconds) override {} // SQLite has no statement timeout

	void BindParameter(size_t index, const std::string &value) override;
	void BindParameter(size_t index, int value) override;
	void BindParameter(size_t index, long long value) override;
	void BindParameter(size_t index, double value) override;
	void BindParameter(size_t index, bool value) override;
	void BindNullParameter(size_t index) override;

	std::unique_ptr<IResultSet> ExecuteQuery() override;
	int ExecuteUpdate() override;
	bool Execute() override;

	// AddBatch — snapshot current params; ExecuteBatch — run each set in a loop
	void AddBatch() override;
	std::vector<int> ExecuteBatch() override;

	void ClearParameters() override;
	void Close() override;

  private:
	// Parameter variant (mirrors sqlite3 bind types)
	enum class ParamType
	{
		Text,
		Int,
		Int64,
		Real,
		Null
	};

	struct Param
	{
		ParamType type = ParamType::Null;
		std::string text;
		long long int64Val = 0;
		double realVal = 0.0;
	};

	sqlite3_stmt *PrepareStmt();
	void BindAll(sqlite3_stmt *stmt, const std::vector<Param> &params);
	void CheckRC(int rc, const char *op) const;

	// Build and store a numeric Param slot. Floating-point types go to realVal;
	//          integer types (int, long long) go to int64Val via implicit promotion.
	template<ParamType TypeTag, typename T>
	void SetNumParam(size_t index, T value)
	{
		if (mCurrentParams.size() < index) mCurrentParams.resize(index);
		auto &p  = mCurrentParams[index - 1];
		p.type   = TypeTag;
		if constexpr (std::is_floating_point_v<T>) p.realVal  = static_cast<double>(value);
		else                                        p.int64Val = static_cast<long long>(value);
	}

  private:
	sqlite3 *mDb;
	sqlite3_stmt *mStmt; // kept for single-execute path
	std::string mQuery;
	std::vector<Param> mCurrentParams;
	std::vector<std::vector<Param>> mBatchParams;
};

// =============================================================================
// SQLiteConnection — non-owning reference to a shared sqlite3 handle
// =============================================================================

class SQLiteConnection : public IConnection
{
  public:
	explicit SQLiteConnection(sqlite3 *db); // non-owning
	virtual ~SQLiteConnection() = default;

	void Open([[maybe_unused]] const std::string &connectionString) override { mOpen = true; }
	void Close() override { mOpen = false; }
	bool IsOpen() const override { return mOpen; }

	std::unique_ptr<IStatement> CreateStatement() override;

	void BeginTransaction() override;
	void CommitTransaction() override;
	void RollbackTransaction() override;

	int GetLastErrorCode() const override { return mLastErrorCode; }
	std::string GetLastError() const override { return mLastError; }

  private:
	void ExecRaw(const char *sql);

  private:
	sqlite3 *mDb;
	bool mOpen;
	bool mInTransaction;
	int mLastErrorCode;
	std::string mLastError;
};

// =============================================================================
// SQLiteDatabase — opens/closes a SQLite database file
// =============================================================================

class SQLiteDatabase : public IDatabase
{
  public:
	SQLiteDatabase();
	virtual ~SQLiteDatabase();

	// config.mConnectionString is used as the SQLite file path
	void Connect(const DatabaseConfig &config) override;
	void Disconnect() override;
	bool IsConnected() const override;

	std::unique_ptr<IConnection> CreateConnection() override;
	std::unique_ptr<IStatement> CreateStatement() override;

	void BeginTransaction() override;
	void CommitTransaction() override;
	void RollbackTransaction() override;

	DatabaseType GetType() const override { return DatabaseType::SQLite; }
	const DatabaseConfig &GetConfig() const override { return mConfig; }

  private:
	void ExecRaw(const char *sql);

  private:
	DatabaseConfig mConfig;
	sqlite3 *mDb;
	bool mConnected;
};

#else // !HAVE_SQLITE3

// =============================================================================
// SQLiteDatabase stub — throws DatabaseException on Connect()
// =============================================================================

class SQLiteDatabase : public IDatabase
{
  public:
	SQLiteDatabase() : mConnected(false) {}
	virtual ~SQLiteDatabase() = default;

	void Connect(const DatabaseConfig &) override
	{
		throw DatabaseException("SQLite not available: recompile with HAVE_SQLITE3 and link sqlite3");
	}
	void Disconnect() override {}
	bool IsConnected() const override { return false; }

	std::unique_ptr<IConnection> CreateConnection() override
	{
		throw DatabaseException("SQLite not available");
	}
	std::unique_ptr<IStatement> CreateStatement() override
	{
		throw DatabaseException("SQLite not available");
	}

	void BeginTransaction() override {}
	void CommitTransaction() override {}
	void RollbackTransaction() override {}

	DatabaseType GetType() const override { return DatabaseType::SQLite; }
	const DatabaseConfig &GetConfig() const override { return mConfig; }

  private:
	DatabaseConfig mConfig;
	bool mConnected;
};

#endif // HAVE_SQLITE3

} // namespace Database
} // namespace Network
