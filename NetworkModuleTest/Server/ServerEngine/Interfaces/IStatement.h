#pragma once

#include <string>
#include <memory>
#include <vector>

namespace Network::Database {

// Forward declaration
class IResultSet;

/**
 * Abstract statement interface
 */
class IStatement {
public:
    virtual ~IStatement() = default;

    virtual void setQuery(const std::string& query) = 0;
    virtual void setTimeout(int seconds) = 0;

    // Parameter binding
    virtual void bindParameter(size_t index, const std::string& value) = 0;
    virtual void bindParameter(size_t index, int value) = 0;
    virtual void bindParameter(size_t index, long long value) = 0;
    virtual void bindParameter(size_t index, double value) = 0;
    virtual void bindParameter(size_t index, bool value) = 0;
    virtual void bindNullParameter(size_t index) = 0;

    // Query execution
    virtual std::unique_ptr<IResultSet> executeQuery() = 0;
    virtual int executeUpdate() = 0;
    virtual bool execute() = 0;

    // Batch operations
    virtual void addBatch() = 0;
    virtual std::vector<int> executeBatch() = 0;

    virtual void clearParameters() = 0;
    virtual void close() = 0;
};

} // namespace Network::Database
