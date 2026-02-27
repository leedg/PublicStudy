#pragma once

#include "IDatabase.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>

namespace DocDBModule
{

/**
 * Connection pool implementation
 */
class ConnectionPool : public IConnectionPool
{
  private:
	struct PooledConnection
	{
		std::shared_ptr<IConnection> connection;
		std::chrono::steady_clock::time_point lastUsed;
		bool inUse;

		PooledConnection(std::shared_ptr<IConnection> conn)
			: connection(std::move(conn)),
				  lastUsed(std::chrono::steady_clock::now()), inUse(false)
		{
		}
	};

	DatabaseConfig config_;
	std::unique_ptr<IDatabase> database_;
	std::vector<PooledConnection> connections_;
	std::mutex mutex_;
	std::condition_variable condition_;
	std::atomic<bool> initialized_;
	std::atomic<size_t> activeConnections_;

	// Pool settings
	size_t maxPoolSize_;
	size_t minPoolSize_;
	std::chrono::seconds connectionTimeout_;
	std::chrono::seconds idleTimeout_;

	std::shared_ptr<IConnection> createNewConnection();
	void cleanupIdleConnections();

  public:
	ConnectionPool();
	virtual ~ConnectionPool();

	// Initialization
	bool initialize(const DatabaseConfig &config);
	void shutdown();

	// IConnectionPool interface
	std::shared_ptr<IConnection> getConnection() override;
	void returnConnection(std::shared_ptr<IConnection> connection) override;
	void clear() override;
	size_t getActiveConnections() const override;
	size_t getAvailableConnections() const override;

	// Configuration
	void setMaxPoolSize(size_t size);
	void setMinPoolSize(size_t size);
	void setConnectionTimeout(int seconds);
	void setIdleTimeout(int seconds);

	// Status
	bool isInitialized() const { return initialized_.load(); }
	size_t getTotalConnections() const;
};

/**
 * RAII wrapper for automatic connection return to pool
 */
class ScopedConnection
{
  private:
	std::shared_ptr<IConnection> connection_;
	IConnectionPool *pool_;

  public:
	ScopedConnection(std::shared_ptr<IConnection> conn, IConnectionPool *pool)
		: connection_(std::move(conn)), pool_(pool)
	{
	}

	~ScopedConnection()
	{
		if (connection_ && pool_)
		{
			pool_->returnConnection(connection_);
		}
	}

	// Prevent copy
	ScopedConnection(const ScopedConnection &) = delete;
	ScopedConnection &operator=(const ScopedConnection &) = delete;

	// Allow move
	ScopedConnection(ScopedConnection &&other) noexcept
		: connection_(std::move(other.connection_)), pool_(other.pool_)
	{
		other.pool_ = nullptr;
	}

	IConnection *operator->() { return connection_.get(); }
	IConnection &operator*() { return *connection_; }
	const IConnection *operator->() const { return connection_.get(); }
	const IConnection &operator*() const { return *connection_; }

	bool isValid() const
	{
		return connection_ != nullptr && connection_->isOpen();
	}

	IConnection *get() { return connection_.get(); }
	const IConnection *get() const { return connection_.get(); }
};

} // namespace DocDBModule
