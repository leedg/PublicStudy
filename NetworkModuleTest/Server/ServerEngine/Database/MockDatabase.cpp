// English: MockDatabase implementation
// 한글: MockDatabase 구현

#include "MockDatabase.h"

namespace Network
{
namespace Database
{

MockDatabase::MockDatabase() : mConnected(false) {}

void MockDatabase::Connect(const DatabaseConfig &config)
{
	mConfig = config;
	mConnected = true;
}

void MockDatabase::Disconnect()
{
	mConnected = false;
}

bool MockDatabase::IsConnected() const
{
	return mConnected;
}

std::unique_ptr<IConnection> MockDatabase::CreateConnection()
{
	if (!mConnected)
	{
		throw DatabaseException("MockDatabase not connected");
	}
	auto conn = std::make_unique<MockConnection>(mLog, mMutex);
	conn->Open("");
	return conn;
}

std::unique_ptr<IStatement> MockDatabase::CreateStatement()
{
	if (!mConnected)
	{
		throw DatabaseException("MockDatabase not connected");
	}
	return std::make_unique<MockStatement>(mLog, mMutex);
}

std::vector<ExecutedQuery> MockDatabase::GetExecutedQueries() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mLog;
}

void MockDatabase::ClearLog()
{
	std::lock_guard<std::mutex> lock(mMutex);
	mLog.clear();
}

} // namespace Database
} // namespace Network
