// ConnectionPool implementation

#include "ConnectionPool.h"
#include "DatabaseFactory.h"
#include <algorithm>
#include <thread>

namespace Network
{
namespace Database
{

ConnectionPool::ConnectionPool()
	: mInitialized(false), mActiveConnections(0), mMaxPoolSize(10),
		  mMinPoolSize(2), mConnectionTimeout(std::chrono::seconds(30)),
		  mIdleTimeout(std::chrono::seconds(300))
{
}

ConnectionPool::~ConnectionPool() { Shutdown(); }

bool ConnectionPool::Initialize(const DatabaseConfig &config)
{
	std::lock_guard<std::mutex> lock(mMutex);

	if (mInitialized.load())
	{
		return true;
	}

	mConfig = config;
	mMaxPoolSize = config.mMaxPoolSize;
	mMinPoolSize = config.mMinPoolSize;

	// Create database instance
	mDatabase = DatabaseFactory::CreateDatabase(config.mType);
	if (!mDatabase)
	{
		return false;
	}

	try
	{
		mDatabase->Connect(config);

		// Pre-create minimum connections
		for (size_t i = 0; i < mMinPoolSize; ++i)
		{
			auto pConn = CreateNewConnection();
			if (pConn)
			{
				mConnections.emplace_back(pConn);
			}
		}

		mInitialized.store(true);
		return true;
	}
	catch (const DatabaseException &)
	{
		mConnections.clear();
		return false;
	}
}

void ConnectionPool::Shutdown()
{
	if (!mInitialized.load())
	{
		return;
	}

	// Wait for all connections to be returned
	{
		std::unique_lock<std::mutex> lock(mMutex);
		mCondition.wait_for(lock, std::chrono::seconds(5),
							[this] { return mActiveConnections.load() == 0; });
	}

	// Close all connections and disconnect database under a single lock.
	//          Call ClearLocked() (not Clear()) to avoid re-locking the non-recursive
	//          mutex — re-locking std::mutex is undefined behaviour (typically deadlocks).
	{
		std::lock_guard<std::mutex> lock(mMutex);
		ClearLocked();

		if (mDatabase)
		{
			mDatabase->Disconnect();
			mDatabase.reset();
		}
	}

	mInitialized.store(false);
}

std::shared_ptr<IConnection> ConnectionPool::CreateNewConnection()
{
	if (!mDatabase || !mDatabase->IsConnected())
	{
		throw DatabaseException("Database not connected");
	}

	auto pConn = mDatabase->CreateConnection();
	if (!pConn)
	{
		throw DatabaseException("Failed to create connection");
	}

	pConn->Open(mConfig.mConnectionString);
	return std::shared_ptr<IConnection>(std::move(pConn));
}

std::shared_ptr<IConnection> ConnectionPool::GetConnection()
{
	if (!mInitialized.load())
	{
		throw DatabaseException("Connection pool not initialized");
	}

	std::unique_lock<std::mutex> lock(mMutex);

	// Wait for available connection or timeout
	bool acquired = mCondition.wait_for(
		lock, mConnectionTimeout,
		[this]
		{
			// Check if any connection is available
			for (auto &pooled : mConnections)
			{
				if (!pooled.mInUse && pooled.mConnection->IsOpen())
				{
					return true;
				}
			}

			// Can we create a new connection?
			return mConnections.size() < mMaxPoolSize;
		});

	if (!acquired)
	{
		throw DatabaseException(
			"Connection pool timeout - no connections available");
	}

	// Try to find an existing free connection
	for (auto &pooled : mConnections)
	{
		if (!pooled.mInUse && pooled.mConnection->IsOpen())
		{
			pooled.mInUse = true;
			pooled.mLastUsed = std::chrono::steady_clock::now();
			mActiveConnections.fetch_add(1);
			return pooled.mConnection;
		}
	}

	// Create a new connection if under limit
	if (mConnections.size() < mMaxPoolSize)
	{
		auto pConn = CreateNewConnection();
		mConnections.emplace_back(pConn);
		mConnections.back().mInUse = true;
		mActiveConnections.fetch_add(1);
		return pConn;
	}

	throw DatabaseException("No connections available");
}

void ConnectionPool::ReturnConnection(std::shared_ptr<IConnection> pConnection)
{
	if (!pConnection)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(mMutex);

	for (auto &pooled : mConnections)
	{
		if (pooled.mConnection == pConnection)
		{
			pooled.mInUse = false;
			pooled.mLastUsed = std::chrono::steady_clock::now();
			mActiveConnections.fetch_sub(1);
			mCondition.notify_one();
			return;
		}
	}
}

void ConnectionPool::ClearLocked()
{
	// Caller must already hold mMutex — no lock acquired here.
	for (auto it = mConnections.begin(); it != mConnections.end();)
	{
		if (!it->mInUse)
		{
			it->mConnection->Close();
			it = mConnections.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void ConnectionPool::Clear()
{
	std::lock_guard<std::mutex> lock(mMutex);
	ClearLocked();
}

size_t ConnectionPool::GetActiveConnections() const
{
	return mActiveConnections.load();
}

size_t ConnectionPool::GetAvailableConnections() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mMutex));
	size_t available = 0;
	for (const auto &pooled : mConnections)
	{
		if (!pooled.mInUse && pooled.mConnection->IsOpen())
		{
			++available;
		}
	}
	return available;
}

void ConnectionPool::SetMaxPoolSize(size_t size)
{
	std::lock_guard<std::mutex> lock(mMutex);
	mMaxPoolSize = size;
}

void ConnectionPool::SetMinPoolSize(size_t size)
{
	std::lock_guard<std::mutex> lock(mMutex);
	mMinPoolSize = size;
}

void ConnectionPool::SetConnectionTimeout(int seconds)
{
	mConnectionTimeout = std::chrono::seconds(seconds);
}

void ConnectionPool::SetIdleTimeout(int seconds)
{
	mIdleTimeout = std::chrono::seconds(seconds);
}

size_t ConnectionPool::GetTotalConnections() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(mMutex));
	return mConnections.size();
}

void ConnectionPool::CleanupIdleConnections()
{
	std::lock_guard<std::mutex> lock(mMutex);

	auto now = std::chrono::steady_clock::now();

	for (auto it = mConnections.begin(); it != mConnections.end();)
	{
		if (!it->mInUse)
		{
			auto idleDuration =
				std::chrono::duration_cast<std::chrono::seconds>(now -
																 it->mLastUsed);

			if (idleDuration > mIdleTimeout &&
				mConnections.size() > mMinPoolSize)
			{
				it->mConnection->Close();
				it = mConnections.erase(it);
				continue;
			}
		}
		++it;
	}
}

} // namespace Database
} // namespace Network
