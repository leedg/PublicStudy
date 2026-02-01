#pragma once

#include <string>

namespace Network::Database {

/**
 * Abstract result set interface
 */
class IResultSet {
public:
    virtual ~IResultSet() = default;

    virtual bool next() = 0;
    virtual bool isNull(size_t columnIndex) = 0;
    virtual bool isNull(const std::string& columnName) = 0;

    // Data retrieval methods
    virtual std::string getString(size_t columnIndex) = 0;
    virtual std::string getString(const std::string& columnName) = 0;

    virtual int getInt(size_t columnIndex) = 0;
    virtual int getInt(const std::string& columnName) = 0;

    virtual long long getLong(size_t columnIndex) = 0;
    virtual long long getLong(const std::string& columnName) = 0;

    virtual double getDouble(size_t columnIndex) = 0;
    virtual double getDouble(const std::string& columnName) = 0;

    virtual bool getBool(size_t columnIndex) = 0;
    virtual bool getBool(const std::string& columnName) = 0;

    // Metadata
    virtual size_t getColumnCount() const = 0;
    virtual std::string getColumnName(size_t columnIndex) const = 0;
    virtual size_t findColumn(const std::string& columnName) const = 0;

    virtual void close() = 0;
};

} // namespace Network::Database
