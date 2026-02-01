#pragma once

#include <string>
#include <memory>

namespace Network::Database {

// Forward declaration
class IStatement;

/**
 * Abstract connection interface
 */
class IConnection {
public:
    virtual ~IConnection() = default;

    virtual void open(const std::string& connectionString) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    virtual std::unique_ptr<IStatement> createStatement() = 0;
    virtual void beginTransaction() = 0;
    virtual void commitTransaction() = 0;
    virtual void rollbackTransaction() = 0;

    virtual int getLastErrorCode() const = 0;
    virtual std::string getLastError() const = 0;
};

} // namespace Network::Database
