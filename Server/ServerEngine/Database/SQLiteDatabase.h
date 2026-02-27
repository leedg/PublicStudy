#pragma once

// English: SQLite implementation of database interfaces
//          Compile with HAVE_SQLITE3 defined (and link sqlite3) for full support.
//          Without HAVE_SQLITE3 a stub class that throws on Connect() is provided
//          so the rest of the build remains unchanged.
// 한글: 데이터베이스 인터페이스의 SQLite 구현
//       HAVE_SQLITE3 정의 시 (및 sqlite3 링크 시) 전체 지원.
//       HAVE_SQLITE3 없으면 Connect()에서 예외를 던지는 스텁 클래스 제공.

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IConnection.h"
#include "../Interfaces/IDatabase.h"
#include "../Interfaces/IResultSet.h"
#include "../Interfaces/IStatement.h"
#include <memory>
#include <string>
#include <vector>

#ifdef HAVE_SQLITE3
#include <sqlite3.h>
#endif

namespace Network
{
namespace Database
{

#ifdef HAVE_SQLITE3

// English: Forward declarations
// 한글: 전방 선언
class SQLiteConnection;
class SQLiteStatement;
class SQLiteResultSet;

// =============================================================================
// English: SQLiteResultSet — wraps sqlite3_stmt result iteration
// 한글: SQLiteResultSet — sqlite3_stmt 결과 이터레이션 래퍼
// =============================================================================

class SQLiteResultSet : public IResultSet
{
  public:
	// English: Takes ownership of the prepared statement
	// 한글: 준비된 구문의 소유권 획득
	explicit SQLiteResultSet(sqlite3_stmt *stmt);
	virtual ~SQLiteResultSet();

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
	void LoadColumnNames();
	int ResolveColumn(const std::string &columnName) const;

  private:
	sqlite3_stmt *mStmt;
	bool mDone;
	std::vector<std::string> mColumnNames;
};

// =============================================================================
// English: SQLiteStatement — prepares and executes SQL against a sqlite3 handle
// 한글: SQLiteStatement — sqlite3 핸들에 대해 SQL 준비 및 실행
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

	// English: AddBatch — snapshot current params; ExecuteBatch — run each set in a loop
	// 한글: AddBatch — 현재 파라미터 스냅샷; ExecuteBatch — 각 파라미터 세트 루프 실행
	void AddBatch() override;
	std::vector<int> ExecuteBatch() override;

	void ClearParameters() override;
	void Close() override;

  private:
	// English: Parameter variant (mirrors sqlite3 bind types)
	// 한글: 파라미터 타입 변형 (sqlite3 bind 타입 대응)
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

  private:
	sqlite3 *mDb;
	sqlite3_stmt *mStmt; // kept for single-execute path
	std::string mQuery;
	std::vector<Param> mCurrentParams;
	std::vector<std::vector<Param>> mBatchParams;
};

// =============================================================================
// English: SQLiteConnection — non-owning reference to a shared sqlite3 handle
// 한글: SQLiteConnection — 공유 sqlite3 핸들에 대한 non-owning 참조
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
// English: SQLiteDatabase — opens/closes a SQLite database file
// 한글: SQLiteDatabase — SQLite 데이터베이스 파일 열기/닫기
// =============================================================================

class SQLiteDatabase : public IDatabase
{
  public:
	SQLiteDatabase();
	virtual ~SQLiteDatabase();

	// English: config.mConnectionString is used as the SQLite file path
	// 한글: config.mConnectionString을 SQLite 파일 경로로 사용
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
// English: SQLiteDatabase stub — throws DatabaseException on Connect()
// 한글: SQLiteDatabase 스텁 — Connect() 호출 시 DatabaseException 발생
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
