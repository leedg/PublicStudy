#pragma once

// English: ODBC implementation of database interfaces
// 한글: 데이터베이스 인터페이스의 ODBC 구현

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
// 한글: ODBC 헤더가 필요로 하는 Windows 타입을 먼저 정의한다.
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <vector>

namespace Network
{
namespace Database
{

// English: Forward declarations
// 한글: 전방 선언
class ODBCConnection;
class ODBCStatement;
class ODBCResultSet;

// =============================================================================
// English: ODBCDatabase class
// 한글: ODBCDatabase 클래스
// =============================================================================

/**
 * English: ODBC implementation of IDatabase
 * 한글: IDatabase의 ODBC 구현
 */
class ODBCDatabase : public IDatabase
{
  public:
	ODBCDatabase();
	virtual ~ODBCDatabase();

	// English: IDatabase interface
	// 한글: IDatabase 인터페이스
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
	// 한글: 헬퍼 메서드
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
// English: ODBCConnection class
// 한글: ODBCConnection 클래스
// =============================================================================

/**
 * English: ODBC implementation of IConnection
 * 한글: IConnection의 ODBC 구현
 */
class ODBCConnection : public IConnection
{
  public:
	explicit ODBCConnection(SQLHENV env);
	virtual ~ODBCConnection();

	// English: IConnection interface
	// 한글: IConnection 인터페이스
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
	// 한글: 헬퍼 메서드
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
// English: ODBCStatement class
// 한글: ODBCStatement 클래스
// =============================================================================

/**
 * English: ODBC implementation of IStatement
 * 한글: IStatement의 ODBC 구현
 */
class ODBCStatement : public IStatement
{
  public:
	// English: conn must remain alive for the lifetime of this statement.
	//          ownerConn (optional) transfers ownership when a statement is created
	//          via IDatabase::CreateStatement() — keeps the per-statement connection alive.
	// 한글: conn은 이 statement의 생존 기간 동안 유효해야 함.
	//       ownerConn(선택)은 IDatabase::CreateStatement()에서 문장별 연결 소유권 이전용.
	explicit ODBCStatement(SQLHDBC conn,
	                       std::unique_ptr<ODBCConnection> ownerConn = nullptr);
	virtual ~ODBCStatement();

	// English: IStatement interface
	// 한글: IStatement 인터페이스
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
	// 한글: 헬퍼 메서드
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string GetSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);
	void BindParameters();

  private:
	// English: Typed parameter value — stores the native C value for each bind type.
	//          SQLBindParameter receives a pointer into this struct; mParams must not
	//          be modified between BindParameters() and SQLExecDirectA().
	// 한글: 타입별 파라미터 값 — 각 바인딩 타입의 네이티브 C 값을 저장.
	//       SQLBindParameter는 이 구조체 내부 포인터를 받으므로 BindParameters()와
	//       SQLExecDirectA() 사이에 mParams를 수정해서는 안 됨.
	struct ParamValue
	{
		enum class Type { Text, Int, Int64, Double, Bool, Null } type = Type::Null;
		std::string text;
		int         intVal    = 0;
		long long   int64Val  = 0;
		double      doubleVal = 0.0;
		SQLLEN      indicator = SQL_NULL_DATA;
	};

	// English: Batch entry — snapshot of parameters for one batch item
	// 한글: 배치 항목 — 배치 아이템 하나의 파라미터 스냅샷
	struct BatchEntry
	{
		std::vector<ParamValue> params;
	};

	// English: Resize mParams and assign a fixed-size typed slot (int / long long / double).
	//          TypeTag is the ParamValue::Type enum; FieldPtr is a member pointer to the value field.
	// 한글: mParams를 늘리고 고정 크기 타입 슬롯을 채움 (int / long long / double).
	//       TypeTag는 ParamValue::Type 열거형; FieldPtr은 값 필드의 멤버 포인터.
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
	// English: Keeps the per-statement connection alive when created via IDatabase::CreateStatement().
	// 한글: IDatabase::CreateStatement()에서 생성된 경우 연결 수명 유지용.
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
// English: ODBCResultSet class
// 한글: ODBCResultSet 클래스
// =============================================================================

/**
 * English: ODBC implementation of IResultSet
 * 한글: IResultSet의 ODBC 구현
 */
class ODBCResultSet : public IResultSet
{
  public:
	explicit ODBCResultSet(SQLHSTMT stmt);
	virtual ~ODBCResultSet();

	// English: IResultSet interface (name overloads inherited from IResultSet default impl)
	// 한글: IResultSet 인터페이스 (이름 오버로드는 IResultSet 기본 구현 상속)
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
	// English: Per-row column data cache. FetchColumn() populates a slot on first access
	//          and returns cached data on subsequent calls within the same row.
	//          This prevents SQLGetData from being called twice on the same column
	//          (which advances the stream cursor on forward-only result sets).
	//          The cache is invalidated on each Next() call.
	// 한글: 행별 컬럼 데이터 캐시. FetchColumn()이 첫 접근 시 슬롯을 채우고
	//       동일 행 내 이후 호출에는 캐시를 반환.
	//       SQLGetData를 같은 컬럼에 두 번 호출하는 것을 방지
	//       (forward-only 커서에서 스트림 커서가 이동함).
	//       Next() 호출마다 캐시를 무효화.
	struct ColumnData
	{
		bool fetched = false;
		bool isNull  = false;
		std::string value;
	};

	// English: Helper methods
	// 한글: 헬퍼 메서드
	void LoadMetadata();
	const ColumnData &FetchColumn(size_t columnIndex);
	void CheckSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string GetSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);

	// English: Parse a string column value as a numeric type; returns defaultVal on failure.
	// 한글: 문자열 컬럼 값을 숫자 타입으로 변환; 실패 시 defaultVal 반환.
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
