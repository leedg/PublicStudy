#pragma once

// English: OLEDB implementation of database interfaces
// 한글: 데이터베이스 인터페이스의 OLEDB 구현

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IConnection.h"
#include "../Interfaces/IDatabase.h"
#include "../Interfaces/IResultSet.h"
#include "../Interfaces/IStatement.h"
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// 한글: OLEDB 헤더가 필요로 하는 Windows 타입을 먼저 정의한다.
#include <windows.h>
#include <oledb.h>
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace Network
{
namespace Database
{

// English: Forward declarations
// 한글: 전방 선언
class OLEDBConnection;
class OLEDBStatement;
class OLEDBResultSet;

// =============================================================================
// English: OLEDBDatabase class
// 한글: OLEDBDatabase 클래스
// =============================================================================

/**
 * English: OLEDB implementation of IDatabase
 * 한글: IDatabase의 OLEDB 구현
 */
class OLEDBDatabase : public IDatabase
{
  public:
	OLEDBDatabase();
	virtual ~OLEDBDatabase();

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

	DatabaseType GetType() const override { return DatabaseType::OLEDB; }

	const DatabaseConfig &GetConfig() const override { return mConfig; }

  private:
	DatabaseConfig mConfig;
	bool mConnected;
};

// =============================================================================
// English: OLEDBConnection class
// 한글: OLEDBConnection 클래스
// =============================================================================

/**
 * English: OLEDB implementation of IConnection
 * 한글: IConnection의 OLEDB 구현
 */
class OLEDBConnection : public IConnection
{
  public:
	OLEDBConnection();
	virtual ~OLEDBConnection();

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

  private:
	bool mConnected;
	std::string mLastError;
	int mLastErrorCode;
};

// =============================================================================
// English: OLEDBStatement class
// 한글: OLEDBStatement 클래스
// =============================================================================

/**
 * English: OLEDB implementation of IStatement
 * 한글: IStatement의 OLEDB 구현
 */
class OLEDBStatement : public IStatement
{
  public:
	OLEDBStatement();
	virtual ~OLEDBStatement();

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
	std::string mQuery;
	bool mPrepared;
	int mTimeout;
	std::vector<std::string> mParameters;
	std::vector<std::string> mBatch;
};

// =============================================================================
// English: OLEDBResultSet class
// 한글: OLEDBResultSet 클래스
// =============================================================================

/**
 * English: OLEDB implementation of IResultSet
 * 한글: IResultSet의 OLEDB 구현
 */
class OLEDBResultSet : public IResultSet
{
  public:
	OLEDBResultSet();
	virtual ~OLEDBResultSet();

	// English: IResultSet interface
	// 한글: IResultSet 인터페이스
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
	// 한글: 헬퍼 메서드
	void LoadMetadata();

  private:
	bool mHasData;
	std::vector<std::string> mColumnNames;
	bool mMetadataLoaded;
};

} // namespace Database
} // namespace Network
