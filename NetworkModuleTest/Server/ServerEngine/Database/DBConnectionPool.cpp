// English: DBConnectionPool implementation
// ???: DBConnectionPool ?닌뗭겱

#include "DBConnectionPool.h"
#include <iostream>

namespace Network::Database
{

DBConnectionPool &DBConnectionPool::Instance()
{
	static DBConnectionPool instance;
	return instance;
}

bool DBConnectionPool::Initialize(const std::string &connectionString,
								  uint32_t poolSize)
{
	if (mInitialized)
	{
		std::cerr << "[WARN] DBConnectionPool already initialized" << std::endl;
		return false;
	}

	mConnectionString = connectionString;

	for (uint32_t i = 0; i < poolSize; ++i)
	{
		auto connection = std::make_shared<DBConnection>();

		if (connection->Connect(connectionString))
		{
			std::lock_guard<std::mutex> lock(mMutex);
			mConnections.push(connection);
			++mTotalCount;
		}
		else
		{
			std::cerr << "[WARN] Failed to create DB connection #" << i
						  << std::endl;
		}
	}

	mInitialized = true;

	std::cout << "[INFO] DBConnectionPool initialized - connections: "
				  << mTotalCount << std::endl;

	return mTotalCount > 0;
}

void DBConnectionPool::Shutdown()
{
	std::lock_guard<std::mutex> lock(mMutex);

	while (!mConnections.empty())
	{
		auto connection = mConnections.front();
		mConnections.pop();
		connection->Disconnect();
	}

	mTotalCount = 0;
	mInitialized = false;
	std::cout << "[INFO] DBConnectionPool shutdown" << std::endl;
}

DBConnectionRef DBConnectionPool::Acquire()
{
	std::unique_lock<std::mutex> lock(mMutex);

	// English: Wait with timeout to avoid deadlock
	// ???: ?怨뺣굡??獄쎻뫗????袁る퉸 ???袁⑸툡????疫?
	bool result = mCondition.wait_for(lock, std::chrono::seconds(5), [this]()
										  { return !mConnections.empty(); });

	if (!result || mConnections.empty())
	{
		std::cerr
			<< "[WARN] DBConnectionPool: no connection available (timeout)"
			<< std::endl;
		return nullptr;
	}

	DBConnectionRef connection = mConnections.front();
	mConnections.pop();

	return connection;
}

void DBConnectionPool::Release(DBConnectionRef connection)
{
	if (!connection)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> lock(mMutex);
		mConnections.push(connection);
	}

	mCondition.notify_one();
}

size_t DBConnectionPool::GetAvailableCount() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mConnections.size();
}

} // namespace Network::Database
