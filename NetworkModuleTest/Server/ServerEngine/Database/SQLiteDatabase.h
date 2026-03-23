#pragma once

// 데이터베이스 인터페이스의 SQLite 구현.
// SQLite는 외부 서버 없이 단일 파일로 동작하므로 로컬 캐시나
// 설정 저장용 경량 DB로 적합하다.
//
// 빌드 조건:
//   - HAVE_SQLITE3 정의 + sqlite3 링크: 완전 구현 사용
//   - HAVE_SQLITE3 미정의: Connect() 호출 시 DatabaseException을 던지는 스텁 클래스 제공.
//     스텁이 있으므로 HAVE_SQLITE3 없이도 나머지 빌드에 영향을 주지 않는다.

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

// 전방 선언
class SQLiteConnection;
class SQLiteStatement;
class SQLiteResultSet;

// =============================================================================
// SQLiteResultSet — sqlite3_stmt를 래핑하는 결과 집합.
// Close() 시 sqlite3_finalize()로 stmt를 해제한다.
// =============================================================================

class SQLiteResultSet : public IResultSet
{
  public:
	// stmt 소유권을 이전받는다. Close()/소멸자에서 sqlite3_finalize() 호출.
	explicit SQLiteResultSet(sqlite3_stmt *stmt);
	virtual ~SQLiteResultSet();

	// IResultSet 인터페이스 (이름 오버로드는 IResultSet 기본 구현 상속)
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
	// Next()가 SQLITE_ROW를 반환한 동안만 true.
	// Get*() 호출 전에 반드시 true여야 한다 (sqlite3_column_* 호출 조건).
	bool mHasData;
	std::vector<std::string> mColumnNames;
};

// =============================================================================
// SQLiteStatement — sqlite3 핸들을 대상으로 SQL을 준비·실행.
// SetTimeout()은 SQLite에 statement 단위 timeout이 없으므로 no-op.
// =============================================================================

class SQLiteStatement : public IStatement
{
  public:
	explicit SQLiteStatement(sqlite3 *db);
	virtual ~SQLiteStatement();

	void SetQuery(const std::string &query) override;
	void SetTimeout([[maybe_unused]] int seconds) override {} // SQLite에는 statement 단위 timeout 없음

	void BindParameter(size_t index, const std::string &value) override;
	void BindParameter(size_t index, int value) override;
	void BindParameter(size_t index, long long value) override;
	void BindParameter(size_t index, double value) override;
	void BindParameter(size_t index, bool value) override;
	void BindNullParameter(size_t index) override;

	std::unique_ptr<IResultSet> ExecuteQuery() override;
	int ExecuteUpdate() override;
	bool Execute() override;

	// AddBatch — 현재 파라미터 스냅샷; ExecuteBatch — 각 파라미터 셋을 루프 실행
	void AddBatch() override;
	std::vector<int> ExecuteBatch() override;

	void ClearParameters() override;
	void Close() override;

  private:
	// sqlite3 bind 타입에 대응하는 파라미터 변형
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

	// 숫자 타입 Param 슬롯 생성/저장.
	// 부동소수점(float, double)은 realVal, 정수형(int, long long)은 int64Val에 저장.
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
	sqlite3_stmt *mStmt; // 단일 실행 경로용 — ExecuteQuery에서 SQLiteResultSet으로 소유권 이전
	std::string mQuery;
	std::vector<Param> mCurrentParams;
	std::vector<std::vector<Param>> mBatchParams;
};

// =============================================================================
// SQLiteConnection — 공유 sqlite3 핸들에 대한 non-owning 참조.
// SQLiteDatabase가 sqlite3 핸들을 소유하며, 이 클래스는 빌려 쓴다.
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
// SQLiteDatabase — SQLite 파일을 열고 닫는 IDatabase 구현.
//
// WAL(Write-Ahead Logging) 모드를 활성화하는 이유:
//   기본 journal 모드(DELETE)는 쓰기 시 전체 DB 파일에 배타 잠금을 걸어
//   읽기도 블록된다. WAL 모드에서는 쓰기와 읽기가 병렬로 동작하므로
//   ConnectionPool을 통해 여러 SQLiteConnection이 동시에 사용될 때 성능이 향상된다.
// =============================================================================

class SQLiteDatabase : public IDatabase
{
  public:
	SQLiteDatabase();
	virtual ~SQLiteDatabase();

	// config.mConnectionString을 SQLite 파일 경로로 사용.
	// ":memory:" 지정 시 인메모리 DB로 동작.
	// 연결 성공 후 WAL 모드를 자동으로 활성화한다.
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
// SQLiteDatabase 스텁 — HAVE_SQLITE3 미정의 시 사용.
// Connect() 호출 시 DatabaseException을 발생시킨다.
// 이 스텁 덕분에 HAVE_SQLITE3 없이도 나머지 빌드에 영향을 주지 않는다.
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
