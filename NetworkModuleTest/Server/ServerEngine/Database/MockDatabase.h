#pragma once

// In-memory mock database implementation for testing (no external dependencies)

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

// Forward declarations
class MockConnection;
class MockStatement;

// =============================================================================
// ExecutedQuery — record of a query execution (for test verification)
// =============================================================================

struct ExecutedQuery
{
	std::string query;
	std::vector<std::string> parameters;
};

// =============================================================================
// MockResultSet — empty result set (Next() always returns false)
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
// MockStatement — logs every query + parameters, returns success
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

	// AddBatch — snapshot current params to batch list, then clear for next set
	void AddBatch() override
	{
		mBatchEntries.push_back({mQuery, mCurrentParams});
		mCurrentParams.clear();
	}

	// ExecuteBatch — record all batched entries to log, return success codes
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
// MockConnection — tracks open/transaction state, creates MockStatements
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
// MockDatabase — in-memory mock, shared query log for all connections
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

	// Test verification — retrieve all logged query executions
	std::vector<ExecutedQuery> GetExecutedQueries() const;

	// Clear the query log
	void ClearLog();

  private:
	DatabaseConfig mConfig;
	bool mConnected;
	mutable std::mutex mMutex;
	std::vector<ExecutedQuery> mLog;
};

} // namespace Database
} // namespace Network
