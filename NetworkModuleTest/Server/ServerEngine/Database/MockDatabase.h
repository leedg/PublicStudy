#pragma once

// Mock 데이터베이스 구현 — 외부 DB 서버 없이 IDatabase 인터페이스를 완전히 구현.
//
// 주요 사용 사례:
//   1. 단위 테스트: GetExecutedQueries()로 쿼리 실행 내역을 검증.
//   2. 서버 시작 시 SqlModuleBootstrap 우회: DatabaseType::Mock이면
//      bootstrap 상태 테이블 생성/검사를 건너뛰므로 DB 없이 서버 초기화 가능.
//   3. CI 환경: 외부 DB 의존성 없이 빌드/테스트 파이프라인 구동.
//
// MockDatabase는 모든 커넥션이 하나의 쿼리 로그(mLog)를 공유하며,
// mMutex로 멀티스레드 접근을 보호한다.

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

// 전방 선언
class MockConnection;
class MockStatement;

// =============================================================================
// ExecutedQuery — 쿼리 실행 기록 (테스트 검증용)
// =============================================================================

struct ExecutedQuery
{
	std::string query;
	std::vector<std::string> parameters;
};

// =============================================================================
// MockResultSet — 항상 빈 결과 집합 (Next()는 항상 false 반환)
// 실제 데이터가 필요한 테스트에서는 MockStatement를 서브클래싱하거나
// 별도의 테스트 픽스처를 사용할 것.
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
// MockStatement — 쿼리와 파라미터를 로그에 기록하고 항상 성공을 반환.
// ExecuteQuery()는 빈 MockResultSet, ExecuteUpdate()는 1(1행 영향)을 반환한다.
// mLog와 mMutex는 MockDatabase 소유이며 참조로 전달받는다.
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

	// AddBatch — 현재 파라미터를 배치 목록에 저장 후 초기화
	void AddBatch() override
	{
		mBatchEntries.push_back({mQuery, mCurrentParams});
		mCurrentParams.clear();
	}

	// ExecuteBatch — 배치 항목 전체를 로그에 기록하고 각 항목에 대해 1(성공)을 반환
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
// MockConnection — 연결/트랜잭션 상태를 플래그로 추적하고 MockStatement를 생성.
// Open()을 호출하면 즉시 연결 성공 상태가 된다.
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
// MockDatabase — 모든 커넥션이 공유 쿼리 로그(mLog)를 통해 실행 내역을 기록.
// GetExecutedQueries() / ClearLog()로 테스트에서 실행 내역을 검증하거나 초기화한다.
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

	// Mock은 트랜잭션을 시뮬레이션하지 않으므로 no-op
	void BeginTransaction() override {}
	void CommitTransaction() override {}
	void RollbackTransaction() override {}

	DatabaseType GetType() const override { return DatabaseType::Mock; }
	const DatabaseConfig &GetConfig() const override { return mConfig; }

	// 테스트 검증용 — 실행된 모든 쿼리 로그 조회 (thread-safe)
	std::vector<ExecutedQuery> GetExecutedQueries() const;

	// 쿼리 로그 초기화 (thread-safe)
	void ClearLog();

  private:
	DatabaseConfig mConfig;
	bool mConnected;
	mutable std::mutex mMutex;
	std::vector<ExecutedQuery> mLog;
};

} // namespace Database
} // namespace Network
