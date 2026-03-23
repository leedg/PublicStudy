#pragma once

// 데이터베이스 인터페이스의 PostgreSQL(libpq) 직접 구현.
//
// ODBC 대신 libpq를 직접 사용하는 이유:
//   - 크로스 플랫폼: ODBC 드라이버 설치 없이 Windows/Linux 모두 동일 코드로 동작.
//   - 의존성 최소화: psqlODBC DSN 구성 없이 연결 문자열(conninfo/URI) 하나로 연결.
//   - PQexecParams: 파라미터를 별도 배열로 전달하므로 SQL 인젝션 차단이 구조적으로 보장됨.
//   - PostgreSQL 고유 기능(COPY, 알림, 비동기 쿼리 등) 활용이 용이.
//
// 빌드 조건:
//   - HAVE_LIBPQ 정의 + libpq 링크 필요.
//   - HAVE_LIBPQ 미정의 시 이 파일의 클래스들은 존재하지 않으며,
//     DatabaseFactory::CreatePostgreSQLDatabase()가 런타임에 DatabaseException을 던진다.

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
// PostgreSQLResultSet — PGresult*를 래핑하는 커서 기반 결과 집합.
// libpq는 PQexecParams 호출 시 결과 전체를 메모리에 올리므로
// 대용량 결과 집합에서는 서버 커서(커서 FETCH) 방식을 고려할 것.
// =============================================================================

class PostgreSQLResultSet : public IResultSet
{
  public:
	// result 소유권을 이전받는다. Close()/소멸자에서 PQclear() 호출.
	explicit PostgreSQLResultSet(PGresult *result);
	~PostgreSQLResultSet() override;

	bool        Next() override;
	bool        IsNull(size_t columnIndex) override;
	std::string GetString(size_t columnIndex) override;
	int         GetInt(size_t columnIndex) override;
	long long   GetLong(size_t columnIndex) override;
	double      GetDouble(size_t columnIndex) override;
	// PostgreSQL BOOLEAN 컬럼은 't'/'f'를 반환한다 ('1'/'true'도 허용).
	bool        GetBool(size_t columnIndex) override;

	size_t      GetColumnCount() const override;
	std::string GetColumnName(size_t columnIndex) const override;
	size_t      FindColumn(const std::string &columnName) const override;

	void Close() override;

  private:
	void CheckRow() const;

	PGresult *           mResult;
	int                  mNumRows;
	int                  mCurrentRow; // -1: Next() 호출 전
	std::vector<std::string> mColumnNames;
};

// =============================================================================
// PostgreSQLStatement — PQexecParams를 통해 파라미터화 쿼리를 실행.
// 쿼리는 PostgreSQL 스타일 플레이스홀더($1, $2, ...)를 사용해야 한다.
// SetTimeout()은 session-level SET statement_timeout으로 구현된다.
// =============================================================================

class PostgreSQLStatement : public IStatement
{
  public:
	explicit PostgreSQLStatement(PGconn *conn);
	~PostgreSQLStatement() override;

	void SetQuery(const std::string &query) override;
	// SET statement_timeout (세션 레벨, 밀리초 단위)으로 적용.
	// 0이면 타임아웃 없음.
	void SetTimeout(int seconds) override;

	void BindParameter(size_t index, const std::string &value) override;
	void BindParameter(size_t index, int value) override;
	void BindParameter(size_t index, long long value) override;
	void BindParameter(size_t index, double value) override;
	// bool은 PostgreSQL 형식('t'/'f')으로 변환하여 바인딩
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
// PostgreSQLConnection — PQconnectdb로 독립적인 물리 연결을 소유.
// Close() 시 트랜잭션이 열려 있으면 자동으로 ROLLBACK한다.
// =============================================================================

class PostgreSQLConnection : public IConnection
{
  public:
	PostgreSQLConnection();
	~PostgreSQLConnection() override;

	// connectionString: libpq conninfo 문자열("host=... dbname=...") 또는 URI
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
// PostgreSQLDatabase — 공유 PGconn*를 소유하는 IDatabase 구현.
// CreateStatement()는 이 공유 연결을 사용하므로 단일 스레드 환경에 적합.
// 멀티스레드 환경에서는 CreateConnection()으로 독립 연결을 획득하거나
// ConnectionPool을 사용할 것.
// =============================================================================

class PostgreSQLDatabase : public IDatabase
{
  public:
	PostgreSQLDatabase();
	~PostgreSQLDatabase() override;

	// config.mConnectionString: libpq conninfo 문자열 또는 URI
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
