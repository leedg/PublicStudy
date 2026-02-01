#pragma once

#include "DatabaseType_enum.h"
#include "DatabaseConfig.h"
#include <memory>

namespace Network::Database {

// Forward declarations
class IConnection;
class IStatement;

/**
 * Abstract database interface
 */
class IDatabase {
public:
    virtual ~IDatabase() = default;

    virtual void connect(const DatabaseConfig& config) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual std::unique_ptr<IConnection> createConnection() = 0;
    virtual std::unique_ptr<IStatement> createStatement() = 0;

    virtual void beginTransaction() = 0;
    virtual void commitTransaction() = 0;
    virtual void rollbackTransaction() = 0;

    virtual DatabaseType getType() const = 0;
    virtual const DatabaseConfig& getConfig() const = 0;
};

} // namespace Network::Database
