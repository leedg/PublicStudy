#pragma once

#include "IDatabase.h"
#include <string>
#include <vector>
#include <memory>
#include <windows.h>
#include <oledb.h>

namespace DocDBModule {

// Forward declarations
class OLEDBConnection;
class OLEDBStatement;
class OLEDBResultSet;

/**
 * OLEDB implementation of IDatabase
 */
class OLEDBDatabase : public IDatabase {
private:
    DatabaseConfig config_;
    bool connected_;
    
public:
    OLEDBDatabase();
    virtual ~OLEDBDatabase();
    
    // IDatabase interface
    void connect(const DatabaseConfig& config) override;
    void disconnect() override;
    bool isConnected() const override;
    
    std::unique_ptr<IConnection> createConnection() override;
    std::unique_ptr<IStatement> createStatement() override;
    
    void beginTransaction() override;
    void commitTransaction() override;
    void rollbackTransaction() override;
    
    DatabaseType getType() const override { return DatabaseType::OLEDB; }
    const DatabaseConfig& getConfig() const override { return config_; }
};

/**
 * OLEDB implementation of IConnection
 */
class OLEDBConnection : public IConnection {
private:
    bool connected_;
    std::string lastError_;
    int lastErrorCode_;
    
public:
    OLEDBConnection();
    virtual ~OLEDBConnection();
    
    // IConnection interface
    void open(const std::string& connectionString) override;
    void close() override;
    bool isOpen() const override;
    
    std::unique_ptr<IStatement> createStatement() override;
    void beginTransaction() override;
    void commitTransaction() override;
    void rollbackTransaction() override;
    
    int getLastErrorCode() const override { return lastErrorCode_; }
    std::string getLastError() const override { return lastError_; }
};

/**
 * OLEDB implementation of IStatement
 */
class OLEDBStatement : public IStatement {
private:
    std::string query_;
    bool prepared_;
    int timeout_;
    std::vector<std::string> parameters_;
    std::vector<std::string> batch_;
    
public:
    OLEDBStatement();
    virtual ~OLEDBStatement();
    
    // IStatement interface
    void setQuery(const std::string& query) override;
    void setTimeout(int seconds) override;
    
    void bindParameter(size_t index, const std::string& value) override;
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
 * OLEDB implementation of IResultSet
 */
class OLEDBResultSet : public IResultSet {
private:
    bool hasData_;
    std::vector<std::string> columnNames_;
    bool metadataLoaded_;
    
    void loadMetadata();
    
public:
    OLEDBResultSet();
    virtual ~OLEDBResultSet();
    
    // IResultSet interface
    bool next() override;
    bool isNull(size_t columnIndex) override;
    bool isNull(const std::string& columnName) override;
    
    std::string getString(size_t columnIndex) override;
    std::string getString(const std::string& columnName) override;
    
    int getInt(size_t columnIndex) override;
    int getInt(const std::string& columnName) override;
    
    long long getLong(size_t columnIndex) override;
    long long getLong(const std::string& columnName) override;
    
    double getDouble(size_t columnIndex) override;
    double getDouble(const std::string& columnName) override;
    
    bool getBool(size_t columnIndex) override;
    bool getBool(const std::string& columnName) override;
    
    size_t getColumnCount() const override;
    std::string getColumnName(size_t columnIndex) const override;
    size_t findColumn(const std::string& columnName) const override;
    
    void close() override;
};

} // namespace DocDBModule
