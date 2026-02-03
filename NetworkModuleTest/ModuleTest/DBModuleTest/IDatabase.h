#pragma once

#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace DocDBModule
{

// Forward declarations
class IConnection;
class IStatement;
class IResultSet;
class DatabaseException;

/**
 * Database type enumeration
 */
enum class DatabaseType
{
	ODBC,
	OLEDB
};

/**
 * Database configuration structure
 */
struct DatabaseConfig
{
	std::string connectionString;
	DatabaseType type;
	int connectionTimeout = 30;
	int commandTimeout = 30;
	bool autoCommit = true;
	int maxPoolSize = 10;
	int minPoolSize = 1;
};

/**
 * Exception class for database operations
 */
class DatabaseException : public std::exception
{
  private:
	std::string message_;
	int errorCode_;

  public:
	DatabaseException(const std::string &message, int errorCode = 0)
		: message_(message), errorCode_(errorCode)
	{
	}

	const char *what() const noexcept override { return message_.c_str(); }

	int getErrorCode() const { return errorCode_; }
};

/**
 * Abstract database interface
 */
class IDatabase
{
  public:
	virtual ~IDatabase() = default;

	virtual void connect(const DatabaseConfig &config) = 0;
	virtual void disconnect() = 0;
	virtual bool isConnected() const = 0;

	virtual std::unique_ptr<IConnection> createConnection() = 0;
	virtual std::unique_ptr<IStatement> createStatement() = 0;

	virtual void beginTransaction() = 0;
	virtual void commitTransaction() = 0;
	virtual void rollbackTransaction() = 0;

	virtual DatabaseType getType() const = 0;
	virtual const DatabaseConfig &getConfig() const = 0;
};

/**
 * Abstract connection interface
 */
class IConnection
{
  public:
	virtual ~IConnection() = default;

	virtual void open(const std::string &connectionString) = 0;
	virtual void close() = 0;
	virtual bool isOpen() const = 0;

	virtual std::unique_ptr<IStatement> createStatement() = 0;
	virtual void beginTransaction() = 0;
	virtual void commitTransaction() = 0;
	virtual void rollbackTransaction() = 0;

	virtual int getLastErrorCode() const = 0;
	virtual std::string getLastError() const = 0;
};

/**
 * Abstract statement interface
 */
class IStatement
{
  public:
	virtual ~IStatement() = default;

	virtual void setQuery(const std::string &query) = 0;
	virtual void setTimeout(int seconds) = 0;

	// Parameter binding
	virtual void bindParameter(size_t index, const std::string &value) = 0;
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

/**
 * Abstract result set interface
 */
class IResultSet
{
  public:
	virtual ~IResultSet() = default;

	virtual bool next() = 0;
	virtual bool isNull(size_t columnIndex) = 0;
	virtual bool isNull(const std::string &columnName) = 0;

	// Data retrieval methods
	virtual std::string getString(size_t columnIndex) = 0;
	virtual std::string getString(const std::string &columnName) = 0;

	virtual int getInt(size_t columnIndex) = 0;
	virtual int getInt(const std::string &columnName) = 0;

	virtual long long getLong(size_t columnIndex) = 0;
	virtual long long getLong(const std::string &columnName) = 0;

	virtual double getDouble(size_t columnIndex) = 0;
	virtual double getDouble(const std::string &columnName) = 0;

	virtual bool getBool(size_t columnIndex) = 0;
	virtual bool getBool(const std::string &columnName) = 0;

	// Metadata
	virtual size_t getColumnCount() const = 0;
	virtual std::string getColumnName(size_t columnIndex) const = 0;
	virtual size_t findColumn(const std::string &columnName) const = 0;

	virtual void close() = 0;
};

/**
 * Connection pool interface
 */
class IConnectionPool
{
  public:
	virtual ~IConnectionPool() = default;

	virtual std::shared_ptr<IConnection> getConnection() = 0;
	virtual void returnConnection(std::shared_ptr<IConnection> connection) = 0;
	virtual void clear() = 0;
	virtual size_t getActiveConnections() const = 0;
	virtual size_t getAvailableConnections() const = 0;
};

/**
 * Utility functions
 */
namespace Utils
{
std::string
buildODBCConnectionString(const std::map<std::string, std::string> &params);
std::string
buildOLEDBConnectionString(const std::map<std::string, std::string> &params);

// Type-safe parameter binding helpers
template <typename T>
void bindParameterSafe(IStatement *stmt, size_t index, const T &value);

// Specializations
template <>
void bindParameterSafe<std::string>(IStatement *stmt, size_t index,
									const std::string &value);
template <>
void bindParameterSafe<int>(IStatement *stmt, size_t index, const int &value);
template <>
void bindParameterSafe<long long>(IStatement *stmt, size_t index,
								  const long long &value);
template <>
void bindParameterSafe<double>(IStatement *stmt, size_t index,
								   const double &value);
template <>
void bindParameterSafe<bool>(IStatement *stmt, size_t index, const bool &value);
} // namespace Utils

} // namespace DocDBModule
