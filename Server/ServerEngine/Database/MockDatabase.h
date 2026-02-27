#pragma once

// English: In-memory mock database implementation for testing (no external dependencies)
// 한글: 테스트용 인메모리 Mock 데이터베이스 구현 (외부 의존성 없음)

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IConnection.h"
#include "../Interfaces/IDatabase.h"
#include "../Interfaces/IResultSet.h"
#include "../Interfaces/IStatement.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Network
{
namespace Database
{

// English: Forward declarations
// 한글: 전방 선언
class MockConnection;
class MockStatement;

// =============================================================================
// English: ExecutedQuery — record of a query execution (for test verification)
// 한글: ExecutedQuery — 쿼리 실행 기록 (테스트 검증용)
// =============================================================================

struct ExecutedQuery
{
	std::string query;
	std::vector<std::string> parameters;
};

// =============================================================================
// English: MockResultSet — empty result set (Next() always returns false)
// 한글: MockResultSet — 빈 결과 집합 (Next()는 항상 false 반환)
// =============================================================================

class MockResultSet : public IResultSet
{
  public:
	MockResultSet() = default;
	virtual ~MockResultSet() = default;

	bool Next() override { return false; }
	bool IsNull([[maybe_unused]] size_t columnIndex) override { return true; }
	bool IsNull([[maybe_unused]] const std::string &columnName) override { return true; }

	std::string GetString([[maybe_unused]] size_t columnIndex) override { return {}; }
	std::string GetString([[maybe_unused]] const std::string &columnName) override { return {}; }

	int GetInt([[maybe_unused]] size_t columnIndex) override { return 0; }
	int GetInt([[maybe_unused]] const std::string &columnName) override { return 0; }

	long long GetLong([[maybe_unused]] size_t columnIndex) override { return 0; }
	long long GetLong([[maybe_unused]] const std::string &columnName) override { return 0; }

	double GetDouble([[maybe_unused]] size_t columnIndex) override { return 0.0; }
	double GetDouble([[maybe_unused]] const std::string &columnName) override { return 0.0; }

	bool GetBool([[maybe_unused]] size_t columnIndex) override { return false; }
	bool GetBool([[maybe_unused]] const std::string &columnName) override { return false; }

	size_t GetColumnCount() const override { return 0; }
	std::string GetColumnName([[maybe_unused]] size_t columnIndex) const override { return {}; }
	size_t FindColumn([[maybe_unused]] const std::string &columnName) const override { return 0; }

	void Close() override {}
};

// =============================================================================
// English: MockStatement — logs every query + parameters, returns success
// 한글: MockStatement — 쿼리 + 파라미터 로그 기록, 항상 성공 반환
// =============================================================================

class MockStatement : public IStatement
{
  public:
	MockStatement(std::vector<ExecutedQuery> &log, std::mutex &mutex)
		: mLog(log), mMutex(mutex), mTimeout(30)
	{
	}
	virtual ~MockStatement() = default;

	void SetQuery(const std::string &query) override { mQuery = query; }
	void SetTimeout(int seconds) override { mTimeout = seconds; }

	void BindParameter(size_t index, const std::string &value) override
	{
		EnsureSize(index);
		mCurrentParams[index - 1] = value;
	}
	void BindParameter(size_t index, int value) override
	{
		EnsureSize(index);
		mCurrentParams[index - 1] = std::to_string(value);
	}
	void BindParameter(size_t index, long long value) override
	{
		EnsureSize(index);
		mCurrentParams[index - 1] = std::to_string(value);
	}
	void BindParameter(size_t index, double value) override
	{
		EnsureSize(index);
		mCurrentParams[index - 1] = std::to_string(value);
	}
	void BindParameter(size_t index, bool value) override
	{
		EnsureSize(index);
		mCurrentParams[index - 1] = value ? "1" : "0";
	}
	void BindNullParameter(size_t index) override
	{
		EnsureSize(index);
		mCurrentParams[index - 1] = "NULL";
	}

	std::unique_ptr<IResultSet> ExecuteQuery() override
	{
		RecordExecution();
		return std::make_unique<MockResultSet>();
	}

	int ExecuteUpdate() override
	{
		RecordExecution();
		return 1;
	}

	bool Execute() override
	{
		RecordExecution();
		return true;
	}

	// English: AddBatch — snapshot current params to batch list, then clear for next set
	// 한글: AddBatch — 현재 파라미터를 배치 목록에 저장 후 초기화
	void AddBatch() override
	{
		mBatchEntries.push_back({mQuery, mCurrentParams});
		mCurrentParams.clear();
	}

	// English: ExecuteBatch — record all batched entries to log, return success codes
	// 한글: ExecuteBatch — 배치 항목 전체를 로그에 기록, 성공 코드 반환
	std::vector<int> ExecuteBatch() override
	{
		std::vector<int> results;
		{
			std::lock_guard<std::mutex> lock(mMutex);
			for (auto &entry : mBatchEntries)
			{
				mLog.push_back(entry);
				results.push_back(1);
			}
		}
		mBatchEntries.clear();
		return results;
	}

	void ClearParameters() override { mCurrentParams.clear(); }
	void Close() override {}

  private:
	void EnsureSize(size_t index)
	{
		if (mCurrentParams.size() < index)
			mCurrentParams.resize(index);
	}

	void RecordExecution()
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mLog.push_back({mQuery, mCurrentParams});
	}

  private:
	std::vector<ExecutedQuery> &mLog;
	std::mutex &mMutex;
	std::string mQuery;
	std::vector<std::string> mCurrentParams;
	std::vector<ExecutedQuery> mBatchEntries;
	int mTimeout;
};

// =============================================================================
// English: MockConnection — tracks open/transaction state, creates MockStatements
// 한글: MockConnection — 연결/트랜잭션 상태 추적, MockStatement 생성
// =============================================================================

class MockConnection : public IConnection
{
  public:
	MockConnection(std::vector<ExecutedQuery> &log, std::mutex &mutex)
		: mLog(log), mMutex(mutex), mConnected(false), mInTransaction(false),
		  mLastErrorCode(0)
	{
	}
	virtual ~MockConnection() = default;

	void Open([[maybe_unused]] const std::string &connectionString) override { mConnected = true; }
	void Close() override { mConnected = false; }
	bool IsOpen() const override { return mConnected; }

	std::unique_ptr<IStatement> CreateStatement() override
	{
		return std::make_unique<MockStatement>(mLog, mMutex);
	}

	void BeginTransaction() override { mInTransaction = true; }
	void CommitTransaction() override { mInTransaction = false; }
	void RollbackTransaction() override { mInTransaction = false; }

	int GetLastErrorCode() const override { return mLastErrorCode; }
	std::string GetLastError() const override { return mLastError; }

  private:
	std::vector<ExecutedQuery> &mLog;
	std::mutex &mMutex;
	bool mConnected;
	bool mInTransaction;
	int mLastErrorCode;
	std::string mLastError;
};

// =============================================================================
// English: MockDatabase — in-memory mock, shared query log for all connections
// 한글: MockDatabase — 인메모리 Mock, 모든 커넥션이 쿼리 로그 공유
// =============================================================================

class MockDatabase : public IDatabase
{
  public:
	MockDatabase();
	virtual ~MockDatabase() = default;

	void Connect(const DatabaseConfig &config) override;
	void Disconnect() override;
	bool IsConnected() const override;

	std::unique_ptr<IConnection> CreateConnection() override;
	std::unique_ptr<IStatement> CreateStatement() override;

	void BeginTransaction() override {}
	void CommitTransaction() override {}
	void RollbackTransaction() override {}

	DatabaseType GetType() const override { return DatabaseType::Mock; }
	const DatabaseConfig &GetConfig() const override { return mConfig; }

	// English: Test verification — retrieve all logged query executions
	// 한글: 테스트 검증용 — 실행된 모든 쿼리 로그 조회
	std::vector<ExecutedQuery> GetExecutedQueries() const;

	// English: Clear the query log
	// 한글: 쿼리 로그 초기화
	void ClearLog();

  private:
	DatabaseConfig mConfig;
	bool mConnected;
	mutable std::mutex mMutex;
	std::vector<ExecutedQuery> mLog;
};

} // namespace Database
} // namespace Network
