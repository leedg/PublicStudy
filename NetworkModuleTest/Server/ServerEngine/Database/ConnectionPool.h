#pragma once

// Connection pool implementation

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IConnectionPool.h"
#include "../Interfaces/IDatabase.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

namespace Network
{
namespace Database
{

// =============================================================================
// ConnectionPool class
// =============================================================================

/**
 * Connection pool implementation
 */
class ConnectionPool : public IConnectionPool
{
  public:
	ConnectionPool();
	virtual ~ConnectionPool();

	// Initialization
	bool Initialize(const DatabaseConfig &config);
	void Shutdown();

	// IConnectionPool interface
	std::shared_ptr<IConnection> GetConnection() override;
	void ReturnConnection(std::shared_ptr<IConnection> pConnection) override;
	void Clear() override;
	size_t GetActiveConnections() const override;
	size_t GetAvailableConnections() const override;

	// Configuration
	void SetMaxPoolSize(size_t size);
	void SetMinPoolSize(size_t size);
	void SetConnectionTimeout(int seconds);
	void SetIdleTimeout(int seconds);

	// Status
	bool IsInitialized() const { return mInitialized.load(); }

	size_t GetTotalConnections() const;

  private:
	// ClearLocked — Close idle connections WITHOUT acquiring mMutex.
	//          Callers (Clear, Shutdown) must already hold mMutex.
	//          Prevents deadlock when Shutdown() calls Clear() while owning the lock.
	void ClearLocked();

	// Pooled connection structure
	struct PooledConnection
	{
		std::shared_ptr<IConnection> mConnection;
		std::chrono::steady_clock::time_point mLastUsed;
		bool mInUse;

		PooledConnection(std::shared_ptr<IConnection> pConn)
			: mConnection(std::move(pConn)),
				  mLastUsed(std::chrono::steady_clock::now()), mInUse(false)
		{
		}
	};

	// Helper methods
	std::shared_ptr<IConnection> CreateNewConnection();
	void CleanupIdleConnections();

  private:
	DatabaseConfig mConfig;
	std::unique_ptr<IDatabase> mDatabase;
	std::vector<PooledConnection> mConnections;
	std::mutex mMutex;
	std::condition_variable mCondition;
	std::atomic<bool> mInitialized;
	std::atomic<size_t> mActiveConnections;

	// Pool settings
	size_t mMaxPoolSize;
	size_t mMinPoolSize;
	std::chrono::seconds mConnectionTimeout;
	std::chrono::seconds mIdleTimeout;
};

// =============================================================================
// ScopedConnection class
// =============================================================================

/**
 * RAII wrapper for automatic connection return to pool
 */
class ScopedConnection
{
  public:
	ScopedConnection(std::shared_ptr<IConnection> pConn, IConnectionPool *pPool)
		: mConnection(std::move(pConn)), mPool(pPool)
	{
	}

	~ScopedConnection()
	{
		if (mConnection && mPool)
		{
			mPool->ReturnConnection(mConnection);
		}
	}

	// Prevent copy
	ScopedConnection(const ScopedConnection &) = delete;
	ScopedConnection &operator=(const ScopedConnection &) = delete;

	// Allow move
	ScopedConnection(ScopedConnection &&other) noexcept
		: mConnection(std::move(other.mConnection)), mPool(other.mPool)
	{
		other.mPool = nullptr;
	}

	// Access operators
	IConnection *operator->() { return mConnection.get(); }

	IConnection &operator*() { return *mConnection; }

	const IConnection *operator->() const { return mConnection.get(); }

	const IConnection &operator*() const { return *mConnection; }

	// Validation
	bool IsValid() const
	{
		return mConnection != nullptr && mConnection->IsOpen();
	}

	// Direct access
	IConnection *Get() { return mConnection.get(); }

	const IConnection *Get() const { return mConnection.get(); }

  private:
	std::shared_ptr<IConnection> mConnection;
	IConnectionPool *mPool;
};

} // namespace Database
} // namespace Network
