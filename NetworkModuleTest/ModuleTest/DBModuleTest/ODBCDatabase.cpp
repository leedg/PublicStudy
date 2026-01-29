#include "ODBCDatabase.h"
#include <stdexcept>
#include <sstream>

namespace DocDBModule {

// ODBCDatabase Implementation
ODBCDatabase::ODBCDatabase() 
    : environment_(SQL_NULL_HANDLE), connected_(false) {
    initializeEnvironment();
}

ODBCDatabase::~ODBCDatabase() {
    disconnect();
    cleanupEnvironment();
}

void ODBCDatabase::initializeEnvironment() {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &environment_);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        throw DatabaseException("Failed to allocate ODBC environment handle");
    }
    
    ret = SQLSetEnvAttr(environment_, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        cleanupEnvironment();
        throw DatabaseException("Failed to set ODBC version");
    }
}

void ODBCDatabase::cleanupEnvironment() {
    if (environment_ != SQL_NULL_HANDLE) {
        SQLFreeHandle(SQL_HANDLE_ENV, environment_);
        environment_ = SQL_NULL_HANDLE;
    }
}

void ODBCDatabase::checkSQLReturn(SQLRETURN ret, const std::string& operation, 
                                   SQLHANDLE handle, SQLSMALLINT handleType) {
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLCHAR sqlState[6];
        SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
        SQLINTEGER nativeError;
        SQLSMALLINT messageLength;
        
        SQLGetDiagRec(handleType, handle, 1, sqlState, &nativeError, 
                      message, SQL_MAX_MESSAGE_LENGTH, &messageLength);
        
        std::ostringstream oss;
        oss << operation << " failed: " << message << " (SQL State: " << sqlState << ")";
        throw DatabaseException(oss.str(), static_cast<int>(nativeError));
    }
}

void ODBCDatabase::connect(const DatabaseConfig& config) {
    config_ = config;
    auto connection = std::make_unique<ODBCConnection>(environment_);
    connection->open(config.connectionString);
    connected_ = true;
}

void ODBCDatabase::disconnect() {
    connected_ = false;
}

bool ODBCDatabase::isConnected() const {
    return connected_;
}

std::unique_ptr<IConnection> ODBCDatabase::createConnection() {
    if (!connected_) {
        throw DatabaseException("Database not connected");
    }
    return std::make_unique<ODBCConnection>(environment_);
}

std::unique_ptr<IStatement> ODBCDatabase::createStatement() {
    if (!connected_) {
        throw DatabaseException("Database not connected");
    }
    auto connection = createConnection();
    connection->open(config_.connectionString);
    return connection->createStatement();
}

void ODBCDatabase::beginTransaction() {
    auto connection = createConnection();
    connection->open(config_.connectionString);
    connection->beginTransaction();
}

void ODBCDatabase::commitTransaction() {
    auto connection = createConnection();
    connection->open(config_.connectionString);
    connection->commitTransaction();
}

void ODBCDatabase::rollbackTransaction() {
    auto connection = createConnection();
    connection->open(config_.connectionString);
    connection->rollbackTransaction();
}

// ODBCConnection Implementation
ODBCConnection::ODBCConnection(SQLHENV env) 
    : connection_(SQL_NULL_HANDLE), environment_(env), connected_(false), 
      lastErrorCode_(0) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_DBC, environment_, &connection_);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        throw DatabaseException("Failed to allocate ODBC connection handle");
    }
}

ODBCConnection::~ODBCConnection() {
    close();
}

void ODBCConnection::open(const std::string& connectionString) {
    if (connected_) {
        return; // Already connected
    }
    
    SQLCHAR connStrOut[1024];
    SQLSMALLINT connStrOutLength;
    
    SQLRETURN ret = SQLDriverConnect(connection_, nullptr, 
                                    (SQLCHAR*)connectionString.c_str(), SQL_NTS,
                                    connStrOut, sizeof(connStrOut), 
                                    &connStrOutLength, SQL_DRIVER_NOPROMPT);
    
    checkSQLReturn(ret, "Connection", connection_, SQL_HANDLE_DBC);
    connected_ = true;
}

void ODBCConnection::close() {
    if (connection_ != SQL_NULL_HANDLE) {
        if (connected_) {
            SQLDisconnect(connection_);
            connected_ = false;
        }
        SQLFreeHandle(SQL_HANDLE_DBC, connection_);
        connection_ = SQL_NULL_HANDLE;
    }
}

bool ODBCConnection::isOpen() const {
    return connected_;
}

std::unique_ptr<IStatement> ODBCConnection::createStatement() {
    if (!connected_) {
        throw DatabaseException("Connection not open");
    }
    return std::make_unique<ODBCStatement>(connection_);
}

void ODBCConnection::beginTransaction() {
    SQLRETURN ret = SQLSetConnectAttr(connection_, SQL_ATTR_AUTOCOMMIT, 
                                      (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
    checkSQLReturn(ret, "Begin transaction", connection_, SQL_HANDLE_DBC);
}

void ODBCConnection::commitTransaction() {
    SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, connection_, SQL_COMMIT);
    checkSQLReturn(ret, "Commit transaction", connection_, SQL_HANDLE_DBC);
    
    ret = SQLSetConnectAttr(connection_, SQL_ATTR_AUTOCOMMIT, 
                            (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
    checkSQLReturn(ret, "Reset autocommit", connection_, SQL_HANDLE_DBC);
}

void ODBCConnection::rollbackTransaction() {
    SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, connection_, SQL_ROLLBACK);
    checkSQLReturn(ret, "Rollback transaction", connection_, SQL_HANDLE_DBC);
    
    ret = SQLSetConnectAttr(connection_, SQL_ATTR_AUTOCOMMIT, 
                            (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
    checkSQLReturn(ret, "Reset autocommit", connection_, SQL_HANDLE_DBC);
}

void ODBCConnection::checkSQLReturn(SQLRETURN ret, const std::string& operation) {
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        lastError_ = getSQLErrorMessage(connection_, SQL_HANDLE_DBC);
        throw DatabaseException(operation + ": " + lastError_);
    }
}

std::string ODBCConnection::getSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType) {
    SQLCHAR sqlState[6];
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER nativeError;
    SQLSMALLINT messageLength;
    
    SQLGetDiagRec(handleType, handle, 1, sqlState, &nativeError, 
                  message, SQL_MAX_MESSAGE_LENGTH, &messageLength);
    
    lastErrorCode_ = static_cast<int>(nativeError);
    return std::string(reinterpret_cast<char*>(message));
}

} // namespace DocDBModule