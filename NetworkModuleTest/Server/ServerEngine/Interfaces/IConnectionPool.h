#pragma once

#include "IConnection.h"
#include <memory>

namespace Network::Database {

/**
 * Connection pool interface
 */
class IConnectionPool {
public:
    virtual ~IConnectionPool() = default;

    virtual std::shared_ptr<IConnection> getConnection() = 0;
    virtual void returnConnection(std::shared_ptr<IConnection> connection) = 0;
    virtual void clear() = 0;
    virtual size_t getActiveConnections() const = 0;
    virtual size_t getAvailableConnections() const = 0;
};

} // namespace Network::Database
