#pragma once

#include "IDatabase.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <memory>
// 한글: ODBC 헤더가 필요로 하는 Windows 타입을 먼저 정의한다.
#include <windows.h>
#include <sql.h>
#include <sqlext.h>

namespace DocDBModule
{

// Forward declarations
class ODBCConnection;
class ODBCStatement;
class ODBCResultSet;

/**
 * ODBC implementation of IDatabase
 */
class ODBCDatabase : public IDatabase
{
  private:
	DatabaseConfig config_;
	SQLHENV environment_;
	bool connected_;

	void initializeEnvironment();
	void cleanupEnvironment();
	void checkSQLReturn(SQLRETURN ret, const std::string &operation,
						SQLHANDLE handle, SQLSMALLINT handleType);

  public:
	ODBCDatabase();
	virtual ~ODBCDatabase();

	// IDatabase interface
	void connect(const DatabaseConfig &config) override;
	void disconnect() override;
	bool isConnected() const override;

	std::unique_ptr<IConnection> createConnection() override;
	std::unique_ptr<IStatement> createStatement() override;

	void beginTransaction() override;
	void commitTransaction() override;
	void rollbackTransaction() override;

	DatabaseType getType() const override { return DatabaseType::ODBC; }
	const DatabaseConfig &getConfig() const override { return config_; }

	SQLHENV getEnvironment() const { return environment_; }
};

/**
 * ODBC implementation of IConnection
 */
class ODBCConnection : public IConnection
{
  private:
	SQLHDBC connection_;
	SQLHENV environment_;
	bool connected_;
	std::string lastError_;
	int lastErrorCode_;

	void checkSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string getSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);

  public:
	explicit ODBCConnection(SQLHENV env);
	virtual ~ODBCConnection();

	// IConnection interface
	void open(const std::string &connectionString) override;
	void close() override;
	bool isOpen() const override;

	std::unique_ptr<IStatement> createStatement() override;
	void beginTransaction() override;
	void commitTransaction() override;
	void rollbackTransaction() override;

	int getLastErrorCode() const override { return lastErrorCode_; }
	std::string getLastError() const override { return lastError_; }

	SQLHDBC getHandle() const { return connection_; }
};

/**
 * ODBC implementation of IStatement
 */
class ODBCStatement : public IStatement
{
  private:
	SQLHSTMT statement_;
	SQLHDBC connection_;
	std::string query_;
	std::vector<std::string> parameters_;
	std::vector<SQLLEN> parameterLengths_;
	bool prepared_;
	int timeout_;

	void checkSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string getSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);
	void bindParameters();

  public:
	explicit ODBCStatement(SQLHDBC conn);
	virtual ~ODBCStatement();

	// IStatement interface
	void setQuery(const std::string &query) override;
	void setTimeout(int seconds) override;

	void bindParameter(size_t index, const std::string &value) override;
	void bindParameter(size_t index, int value) override;
	void bindParameter(size_t index, long long value) override;
	void bindParameter(size_t index, double value) override;
	void bindParameter(size_t index, bool value) override;
	void bindNullParameter(size_t index) override;

	std::unique_ptr<IResultSet> executeQuery() override;
	int executeUpdate() override;
	bool execute() override;

	void addBatch() override;
	std::vector<int> executeBatch() override;

	void clearParameters() override;
	void close() override;
};

/**
 * ODBC implementation of IResultSet
 */
class ODBCResultSet : public IResultSet
{
  private:
	SQLHSTMT statement_;
	bool hasData_;
	std::vector<std::string> columnNames_;
	std::vector<SQLSMALLINT> columnTypes_;
	std::vector<SQLULEN> columnSizes_;
	bool metadataLoaded_;

	void loadMetadata();
	void checkSQLReturn(SQLRETURN ret, const std::string &operation);
	std::string getSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType);

  public:
	explicit ODBCResultSet(SQLHSTMT stmt);
	virtual ~ODBCResultSet();

	// IResultSet interface
	bool next() override;
	bool isNull(size_t columnIndex) override;
	bool isNull(const std::string &columnName) override;

	std::string getString(size_t columnIndex) override;
	std::string getString(const std::string &columnName) override;

	int getInt(size_t columnIndex) override;
	int getInt(const std::string &columnName) override;

	long long getLong(size_t columnIndex) override;
	long long getLong(const std::string &columnName) override;

	double getDouble(size_t columnIndex) override;
	double getDouble(const std::string &columnName) override;

	bool getBool(size_t columnIndex) override;
	bool getBool(const std::string &columnName) override;

	size_t getColumnCount() const override;
	std::string getColumnName(size_t columnIndex) const override;
	size_t findColumn(const std::string &columnName) const override;

	void close() override;
};

} // namespace DocDBModule
