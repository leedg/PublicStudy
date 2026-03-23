#pragma once

// 데이터베이스 인터페이스의 ODBC 구현.
// ODBC 선택 이유: Windows 환경에서 DSN 설정만으로 SQL Server, MySQL, Oracle 등
// 다양한 DB에 동일한 코드로 연결할 수 있다. 드라이버 설치만으로 백엔드를 교체할 수 있어
// 다중 DB 지원이 필요한 서버 엔진에 적합하다.
// OLE DB 대비 ODBC는 크로스 플랫폼 표준이지만, 이 구현은 Windows 전용 헤더에 의존한다.

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
// ODBC 헤더가 필요로 하는 Windows 타입을 먼저 정의한다.
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <vector>

namespace Network
{
namespace Database
{

// 전방 선언
class ODBCConnection;
class ODBCStatement;
class ODBCResultSet;

// =============================================================================
// ODBCDatabase — IDatabase의 ODBC 구현
// =============================================================================

class ODBCDatabase : public IDatabase
{
  public:
	ODBCDatabase();
	virtual ~ODBCDatabase();

	void Connect(const DatabaseConfig &config) override;
	void Disconnect() override;
	bool IsConnected() const override;

	std::unique_ptr<IConnection> CreateConnection() override;
	std::unique_ptr<IStatement> CreateStatement() override;

	// 트랜잭션 상태는 연결 단위이므로 이 메서드들은 항상 DatabaseException을 발생시킨다.
	// CreateConnection()으로 얻은 IConnection에서 BeginTransaction()을 사용할 것.
	void BeginTransaction() override;
	void CommitTransaction() override;
	void RollbackTransaction() override;

	DatabaseType GetType() const override { return DatabaseType::ODBC; }

	const DatabaseConfig &GetConfig() const override { return mConfig; }

	SQLHENV GetEnvironment() const { return mEnvironment; }

  private:
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
// ODBCConnection — IConnection의 ODBC 구현
// =============================================================================

class ODBCConnection : public IConnection
{
  public:
	explicit ODBCConnection(SQLHENV env);
	virtual ~ODBCConnection();

	void Open(const std::string &connectionString) override;
	void Close() override;
	bool IsOpen() const override;

	std::unique_ptr<IStatement> CreateStatement() override;

	// BeginTransaction: SQL_AUTOCOMMIT_OFF 설정
	// CommitTransaction/RollbackTransaction: SQLEndTran 후 SQL_AUTOCOMMIT_ON 복구
	void BeginTransaction() override;
	void CommitTransaction() override;
	void RollbackTransaction() override;

	int GetLastErrorCode() const override { return mLastErrorCode; }

	std::string GetLastError() const override { return mLastError; }

	SQLHDBC GetHandle() const { return mConnection; }

  private:
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
// ODBCStatement — IStatement의 ODBC 구현
// =============================================================================

class ODBCStatement : public IStatement
{
  public:
	// conn: statement 생존 기간 동안 유효한 연결 핸들 (non-owning).
	// ownerConn (선택): IDatabase::CreateStatement()에서 생성된 경우
	//   statement 전용 연결의 소유권을 이전받아 연결 수명을 statement와 함께 관리한다.
	explicit ODBCStatement(SQLHDBC conn,
	                       std::unique_ptr<ODBCConnection> ownerConn = nullptr);
	virtual ~ODBCStatement();

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
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string GetSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);
	void BindParameters();

  private:
	// 타입별 파라미터 값 — 각 바인딩 타입의 네이티브 C 값을 저장.
	// SQLBindParameter는 이 구조체 내부 포인터를 받으므로,
	// BindParameters()와 SQLExecDirectA() 사이에 mParams를 수정해서는 안 된다.
	// (문자열 경유 변환을 피해 int/long/double을 네이티브 타입으로 바인딩)
	struct ParamValue
	{
		enum class Type { Text, Int, Int64, Double, Bool, Null } type = Type::Null;
		std::string text;
		int         intVal    = 0;
		long long   int64Val  = 0;
		double      doubleVal = 0.0;
		SQLLEN      indicator = SQL_NULL_DATA;
	};

	// 배치 항목 — 배치 아이템 하나의 파라미터 스냅샷
	struct BatchEntry
	{
		std::vector<ParamValue> params;
	};

	// mParams를 늘리고 고정 크기 타입 슬롯을 채운다 (int / long long / double).
	// TypeTag는 ParamValue::Type 열거형; FieldPtr은 값 필드의 멤버 포인터.
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
	// IDatabase::CreateStatement()에서 생성된 경우 연결 수명 유지용
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
// ODBCResultSet — IResultSet의 ODBC 구현
// =============================================================================

class ODBCResultSet : public IResultSet
{
  public:
	explicit ODBCResultSet(SQLHSTMT stmt);
	virtual ~ODBCResultSet();

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
	// 행별 컬럼 데이터 캐시.
	// FetchColumn()이 첫 접근 시 SQLGetData를 호출해 슬롯을 채우고,
	// 같은 행 내 이후 호출에는 캐시를 반환한다.
	// 이유: forward-only 커서에서 SQLGetData를 같은 컬럼에 두 번 호출하면
	//       스트림 커서가 이동해 데이터를 잃는다.
	// Next() 호출 시 캐시를 전체 무효화한다.
	struct ColumnData
	{
		bool fetched = false;
		bool isNull  = false;
		std::string value;
	};

	void LoadMetadata();
	const ColumnData &FetchColumn(size_t columnIndex);
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string GetSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);

	// 문자열 컬럼 값을 숫자 타입으로 변환; 실패 시 defaultVal 반환.
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
