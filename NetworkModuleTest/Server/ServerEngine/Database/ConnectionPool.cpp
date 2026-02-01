#include "ConnectionPool.h"
#include "DatabaseFactory.h"
#include <algorithm>
#include <thread>

namespace Network::Database {

ConnectionPool::ConnectionPool()
    : initialized_(false)
    , activeConnections_(0)
    , maxPoolSize_(10)
    , minPoolSize_(2)
    , connectionTimeout_(std::chrono::seconds(30))
    , idleTimeout_(std::chrono::seconds(300))
{}

ConnectionPool::~ConnectionPool() {
    shutdown();
}

bool ConnectionPool::initialize(const DatabaseConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_.load()) {
        return true;
    }

    config_ = config;
    maxPoolSize_ = config.maxPoolSize;
    minPoolSize_ = config.minPoolSize;

    // Create database instance
    database_ = DatabaseFactory::createDatabase(config.type);
    if (!database_) {
        return false;
    }

    try {
        database_->connect(config);

        // Pre-create minimum connections
        for (size_t i = 0; i < minPoolSize_; ++i) {
            auto conn = createNewConnection();
            if (conn) {
                connections_.emplace_back(conn);
            }
        }

        initialized_.store(true);
        return true;
    }
    catch (const DatabaseException&) {
        return false;
    }
}

void ConnectionPool::shutdown() {
    if (!initialized_.load()) {
        return;
    }

    // Wait for all connections to be returned
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock,
                            std::chrono::seconds(5),
                            [this] { return activeConnections_.load() == 0; });
    }

    // Close all connections
    std::lock_guard<std::mutex> lock(mutex_);
    clear();

    // Disconnect database
    if (database_) {
        database_->disconnect();
        database_.reset();
    }

    initialized_.store(false);
}

std::shared_ptr<IConnection> ConnectionPool::createNewConnection() {
    if (!database_ || !database_->isConnected()) {
        throw DatabaseException("Database not connected");
    }

    auto conn = database_->createConnection();
    if (!conn) {
        throw DatabaseException("Failed to create connection");
    }

    conn->open(config_.connectionString);
    return std::shared_ptr<IConnection>(std::move(conn));
}

std::shared_ptr<IConnection> ConnectionPool::getConnection() {
    if (!initialized_.load()) {
        throw DatabaseException("Connection pool not initialized");
    }

    std::unique_lock<std::mutex> lock(mutex_);

    // Wait for available connection or timeout
    bool acquired = condition_.wait_for(lock, connectionTimeout_, [this] {
        // Check if any connection is available
        for (auto& pooled : connections_) {
            if (!pooled.inUse && pooled.connection->isOpen()) {
                return true;
            }
        }

        // Can we create a new connection?
        return connections_.size() < maxPoolSize_;
    });

    if (!acquired) {
        throw DatabaseException("Connection pool timeout - no connections available");
    }

    // Try to find an existing free connection
    for (auto& pooled : connections_) {
        if (!pooled.inUse && pooled.connection->isOpen()) {
            pooled.inUse = true;
            pooled.lastUsed = std::chrono::steady_clock::now();
            activeConnections_.fetch_add(1);
            return pooled.connection;
        }
    }

    // Create a new connection if under limit
    if (connections_.size() < maxPoolSize_) {
        auto conn = createNewConnection();
        connections_.emplace_back(conn);
        connections_.back().inUse = true;
        activeConnections_.fetch_add(1);
        return conn;
    }

    throw DatabaseException("No connections available");
}

void ConnectionPool::returnConnection(std::shared_ptr<IConnection> connection) {
    if (!connection) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& pooled : connections_) {
        if (pooled.connection == connection) {
            pooled.inUse = false;
            pooled.lastUsed = std::chrono::steady_clock::now();
            activeConnections_.fetch_sub(1);
            condition_.notify_one();
            return;
        }
    }
}

void ConnectionPool::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Close all connections that are not in use
    for (auto it = connections_.begin(); it != connections_.end();) {
        if (!it->inUse) {
            it->connection->close();
            it = connections_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t ConnectionPool::getActiveConnections() const {
    return activeConnections_.load();
}

size_t ConnectionPool::getAvailableConnections() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    size_t available = 0;
    for (const auto& pooled : connections_) {
        if (!pooled.inUse && pooled.connection->isOpen()) {
            ++available;
        }
    }
    return available;
}

void ConnectionPool::setMaxPoolSize(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxPoolSize_ = size;
}

void ConnectionPool::setMinPoolSize(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    minPoolSize_ = size;
}

void ConnectionPool::setConnectionTimeout(int seconds) {
    connectionTimeout_ = std::chrono::seconds(seconds);
}

void ConnectionPool::setIdleTimeout(int seconds) {
    idleTimeout_ = std::chrono::seconds(seconds);
}

size_t ConnectionPool::getTotalConnections() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return connections_.size();
}

void ConnectionPool::cleanupIdleConnections() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();

    for (auto it = connections_.begin(); it != connections_.end();) {
        if (!it->inUse) {
            auto idleDuration = std::chrono::duration_cast<std::chrono::seconds>(
                now - it->lastUsed);

            if (idleDuration > idleTimeout_ && connections_.size() > minPoolSize_) {
                it->connection->close();
                it = connections_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

} // namespace Network::Database
