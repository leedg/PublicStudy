#include "ODBCDatabase.h"
#include <stdexcept>
#include <sstream>

namespace Network::Database {

// ODBCDatabase Implementation
ODBCDatabase::ODBCDatabase()
    : environment_(SQL_NULL_HANDLE), connected_(false)
{
    initializeEnvironment();
}

ODBCDatabase::~ODBCDatabase()
{
    disconnect();
    cleanupEnvironment();
}

void ODBCDatabase::initializeEnvironment()
{
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

void ODBCDatabase::cleanupEnvironment()
{
    if (environment_ != SQL_NULL_HANDLE)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, environment_);
        environment_ = SQL_NULL_HANDLE;
    }
}

void ODBCDatabase::checkSQLReturn(SQLRETURN ret, const std::string& operation,
                                   SQLHANDLE handle, SQLSMALLINT handleType)
{
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
    {
        SQLCHAR sqlState[6];
        SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
        SQLINTEGER nativeError;
        SQLSMALLINT messageLength;

        SQLGetDiagRecA(handleType, handle, 1, sqlState, &nativeError,
                       message, SQL_MAX_MESSAGE_LENGTH, &messageLength);

        std::ostringstream oss;
        oss << operation << " failed: " << reinterpret_cast<char*>(message)
            << " (SQL State: " << reinterpret_cast<char*>(sqlState) << ")";
        throw DatabaseException(oss.str(), static_cast<int>(nativeError));
    }
}

void ODBCDatabase::connect(const DatabaseConfig& config)
{
    config_ = config;
    auto connection = std::make_unique<ODBCConnection>(environment_);
    connection->open(config.connectionString);
    connected_ = true;
}

void ODBCDatabase::disconnect()
{
    connected_ = false;
}

bool ODBCDatabase::isConnected() const
{
    return connected_;
}

std::unique_ptr<IConnection> ODBCDatabase::createConnection()
{
    if (!connected_)
    {
        throw DatabaseException("Database not connected");
    }
    return std::make_unique<ODBCConnection>(environment_);
}

std::unique_ptr<IStatement> ODBCDatabase::createStatement()
{
    if (!connected_)
    {
        throw DatabaseException("Database not connected");
    }
    auto connection = createConnection();
    connection->open(config_.connectionString);
    return connection->createStatement();
}

void ODBCDatabase::beginTransaction()
{
    auto connection = createConnection();
    connection->open(config_.connectionString);
    connection->beginTransaction();
}

void ODBCDatabase::commitTransaction()
{
    auto connection = createConnection();
    connection->open(config_.connectionString);
    connection->commitTransaction();
}

void ODBCDatabase::rollbackTransaction()
{
    auto connection = createConnection();
    connection->open(config_.connectionString);
    connection->rollbackTransaction();
}

// ODBCConnection Implementation
ODBCConnection::ODBCConnection(SQLHENV env)
    : connection_(SQL_NULL_HANDLE), environment_(env), connected_(false), 
      lastErrorCode_(0)
{
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_DBC, environment_, &connection_);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
    {
        throw DatabaseException("Failed to allocate ODBC connection handle");
    }
}

ODBCConnection::~ODBCConnection()
{
    close();
}

void ODBCConnection::open(const std::string& connectionString)
{
    if (connected_)
    {
        return; // Already connected
    }

    SQLCHAR connStrOut[1024];
    SQLSMALLINT connStrOutLength;

    SQLRETURN ret = SQLDriverConnectA(connection_, nullptr,
                                      (SQLCHAR*)connectionString.c_str(), SQL_NTS,
                                      connStrOut, sizeof(connStrOut),
                                      &connStrOutLength, SQL_DRIVER_NOPROMPT);

    checkSQLReturn(ret, "Connection");
    connected_ = true;
}

void ODBCConnection::close()
{
    if (connection_ != SQL_NULL_HANDLE)
    {
        if (connected_)
        {
            SQLDisconnect(connection_);
            connected_ = false;
        }
        SQLFreeHandle(SQL_HANDLE_DBC, connection_);
        connection_ = SQL_NULL_HANDLE;
    }
}

bool ODBCConnection::isOpen() const
{
    return connected_;
}

std::unique_ptr<IStatement> ODBCConnection::createStatement()
{
    if (!connected_)
    {
        throw DatabaseException("Connection not open");
    }
    return std::make_unique<ODBCStatement>(connection_);
}

void ODBCConnection::beginTransaction()
{
    SQLRETURN ret = SQLSetConnectAttr(connection_, SQL_ATTR_AUTOCOMMIT,
                                      (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
    checkSQLReturn(ret, "Begin transaction");
}

void ODBCConnection::commitTransaction()
{
    SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, connection_, SQL_COMMIT);
    checkSQLReturn(ret, "Commit transaction");

    ret = SQLSetConnectAttr(connection_, SQL_ATTR_AUTOCOMMIT,
                            (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
    checkSQLReturn(ret, "Reset autocommit");
}

void ODBCConnection::rollbackTransaction()
{
    SQLRETURN ret = SQLEndTran(SQL_HANDLE_DBC, connection_, SQL_ROLLBACK);
    checkSQLReturn(ret, "Rollback transaction");

    ret = SQLSetConnectAttr(connection_, SQL_ATTR_AUTOCOMMIT,
                            (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
    checkSQLReturn(ret, "Reset autocommit");
}

void ODBCConnection::checkSQLReturn(SQLRETURN ret, const std::string& operation)
{
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
    {
        lastError_ = getSQLErrorMessage(connection_, SQL_HANDLE_DBC);
        throw DatabaseException(operation + ": " + lastError_);
    }
}

std::string ODBCConnection::getSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType) {
    SQLCHAR sqlState[6];
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER nativeError;
    SQLSMALLINT messageLength;

    SQLGetDiagRecA(handleType, handle, 1, sqlState, &nativeError,
                   message, SQL_MAX_MESSAGE_LENGTH, &messageLength);

    lastErrorCode_ = static_cast<int>(nativeError);
    return std::string(reinterpret_cast<char*>(message));
}

// ODBCStatement Implementation
ODBCStatement::ODBCStatement(SQLHDBC conn)
    : statement_(SQL_NULL_HANDLE), connection_(conn), prepared_(false), timeout_(30)
{
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, connection_, &statement_);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
    {
        throw DatabaseException("Failed to allocate ODBC statement handle");
    }
}

ODBCStatement::~ODBCStatement()
{
    close();
}

void ODBCStatement::setQuery(const std::string& query)
{
    query_ = query;
    prepared_ = false;
}

void ODBCStatement::setTimeout(int seconds)
{
    timeout_ = seconds;
    SQLRETURN ret = SQLSetStmtAttr(statement_, SQL_ATTR_QUERY_TIMEOUT,
                                  (SQLPOINTER)(intptr_t)timeout_, 0);
    checkSQLReturn(ret, "Set timeout");
}

void ODBCStatement::bindParameter(size_t index, const std::string& value)
{
    size_t newSize = std::max(parameters_.size(), index);
    parameters_.resize(newSize);
    parameters_[index - 1] = value;
    parameterLengths_.resize(newSize);
    parameterLengths_[index - 1] = SQL_NTS;
}

void ODBCStatement::bindParameter(size_t index, int value)
{
    size_t newSize = std::max(parameters_.size(), index);
    parameters_.resize(newSize);
    parameters_[index - 1] = std::to_string(value);
    parameterLengths_.resize(newSize);
    parameterLengths_[index - 1] = 0;
}

void ODBCStatement::bindParameter(size_t index, long long value)
{
    size_t newSize = std::max(parameters_.size(), index);
    parameters_.resize(newSize);
    parameters_[index - 1] = std::to_string(value);
    parameterLengths_.resize(newSize);
    parameterLengths_[index - 1] = 0;
}

void ODBCStatement::bindParameter(size_t index, double value)
{
    size_t newSize = std::max(parameters_.size(), index);
    parameters_.resize(newSize);
    parameters_[index - 1] = std::to_string(value);
    parameterLengths_.resize(newSize);
    parameterLengths_[index - 1] = 0;
}

void ODBCStatement::bindParameter(size_t index, bool value)
{
    size_t newSize = std::max(parameters_.size(), index);
    parameters_.resize(newSize);
    parameters_[index - 1] = value ? "1" : "0";
    parameterLengths_.resize(newSize);
    parameterLengths_[index - 1] = 0;
}

void ODBCStatement::bindNullParameter(size_t index)
{
    size_t newSize = std::max(parameters_.size(), index);
    parameters_.resize(newSize);
    parameters_[index - 1] = "";
    parameterLengths_.resize(newSize);
    parameterLengths_[index - 1] = SQL_NULL_DATA;
}

void ODBCStatement::bindParameters()
{
    for (size_t i = 0; i < parameters_.size(); ++i)
    {
        SQLRETURN ret = SQLBindParameter(statement_, (SQLUSMALLINT)(i + 1), SQL_PARAM_INPUT,
                                        SQL_C_CHAR, SQL_VARCHAR, 0, 0,
                                        (SQLPOINTER)parameters_[i].c_str(),
                                        (SQLLEN)parameters_[i].length(),
                                        &parameterLengths_[i]);
        checkSQLReturn(ret, "Bind parameter");
    }
}

std::unique_ptr<IResultSet> ODBCStatement::executeQuery()
{
    if (!prepared_)
    {
        bindParameters();
        SQLRETURN ret = SQLExecDirectA(statement_, (SQLCHAR*)query_.c_str(), SQL_NTS);
        checkSQLReturn(ret, "Execute query");
        prepared_ = true;
    }
    return std::make_unique<ODBCResultSet>(statement_);
}

int ODBCStatement::executeUpdate()
{
    if (!prepared_)
    {
        bindParameters();
        SQLRETURN ret = SQLExecDirectA(statement_, (SQLCHAR*)query_.c_str(), SQL_NTS);
        checkSQLReturn(ret, "Execute update");
        prepared_ = true;
    }

    SQLLEN rowCount = 0;
    SQLRETURN ret = SQLRowCount(statement_, &rowCount);
    checkSQLReturn(ret, "Get row count");
    return static_cast<int>(rowCount);
}

bool ODBCStatement::execute()
{
    if (!prepared_)
    {
        bindParameters();
        SQLRETURN ret = SQLExecDirectA(statement_, (SQLCHAR*)query_.c_str(), SQL_NTS);
        if (ret == SQL_NO_DATA)
        {
            return false;
        }
        checkSQLReturn(ret, "Execute");
        prepared_ = true;
        return true;
    }
    return true;
}

void ODBCStatement::addBatch()
{
    // ODBC batch implementation - simplified
}

std::vector<int> ODBCStatement::executeBatch()
{
    // ODBC batch execution implementation - simplified
    return std::vector<int>();
}

void ODBCStatement::clearParameters()
{
    parameters_.clear();
    parameterLengths_.clear();
    prepared_ = false;
}

void ODBCStatement::close()
{
    if (statement_ != SQL_NULL_HANDLE)
    {
        SQLFreeHandle(SQL_HANDLE_STMT, statement_);
        statement_ = SQL_NULL_HANDLE;
    }
}

void ODBCStatement::checkSQLReturn(SQLRETURN ret, const std::string& operation)
{
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
    {
        throw DatabaseException(operation + ": " + getSQLErrorMessage(statement_, SQL_HANDLE_STMT));
    }
}

std::string ODBCStatement::getSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType)
{
    SQLCHAR sqlState[6];
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER nativeError;
    SQLSMALLINT messageLength;

    SQLGetDiagRecA(handleType, handle, 1, sqlState, &nativeError, 
                  message, SQL_MAX_MESSAGE_LENGTH, &messageLength);

    return std::string(reinterpret_cast<char*>(message));
}

// ODBCResultSet Implementation
ODBCResultSet::ODBCResultSet(SQLHSTMT stmt)
    : statement_(stmt), hasData_(false), metadataLoaded_(false)
{
    loadMetadata();
}

ODBCResultSet::~ODBCResultSet()
{
    close();
}

void ODBCResultSet::loadMetadata()
{
    if (metadataLoaded_) return;

    SQLSMALLINT columnCount = 0;
    SQLRETURN ret = SQLNumResultCols(statement_, &columnCount);
    checkSQLReturn(ret, "Get column count");

    columnNames_.resize(columnCount);
    columnTypes_.resize(columnCount);
    columnSizes_.resize(columnCount);

    for (SQLSMALLINT i = 0; i < columnCount; ++i)
    {
        SQLCHAR columnName[256];
        SQLSMALLINT nameLength;
        SQLSMALLINT dataType;
        SQLULEN columnSize;
        SQLSMALLINT decimalDigits;
        SQLSMALLINT nullable;

        ret = SQLDescribeColA(statement_, (SQLUSMALLINT)(i + 1), columnName, (SQLSMALLINT)sizeof(columnName),
                             &nameLength, &dataType, &columnSize, &decimalDigits, &nullable);
        checkSQLReturn(ret, "Describe column");

        columnNames_[i] = std::string(reinterpret_cast<char*>(columnName));
        columnTypes_[i] = dataType;
        columnSizes_[i] = columnSize;
    }

    metadataLoaded_ = true;
}

bool ODBCResultSet::next()
{
    SQLRETURN ret = SQLFetch(statement_);
    if (ret == SQL_NO_DATA)
    {
        hasData_ = false;
        return false;
    }
    checkSQLReturn(ret, "Fetch row");
    hasData_ = true;
    return true;
}

bool ODBCResultSet::isNull(size_t columnIndex)
{
    SQLLEN indicator;
    SQLCHAR buffer[1];
    SQLRETURN ret = SQLGetData(statement_, static_cast<SQLUSMALLINT>(columnIndex + 1), SQL_C_CHAR,
                              buffer, 0, &indicator);
    checkSQLReturn(ret, "Get data (null check)");
    return indicator == SQL_NULL_DATA;
}

bool ODBCResultSet::isNull(const std::string& columnName) {
    size_t index = findColumn(columnName);
    return isNull(index);
}

std::string ODBCResultSet::getString(size_t columnIndex) {
    if (!hasData_) {
        throw DatabaseException("No current row");
    }

    std::string value;
    SQLLEN indicator = 0;
    char buffer[4096] = { 0 };

    SQLRETURN ret = SQLGetData(statement_, static_cast<SQLUSMALLINT>(columnIndex + 1), SQL_C_CHAR,
                              buffer, sizeof(buffer), &indicator);
    
    if (indicator == SQL_NULL_DATA) {
        return "";
    }
    
    checkSQLReturn(ret, "Get data (string)");
    
    if (ret == SQL_SUCCESS_WITH_INFO && indicator > static_cast<SQLLEN>(sizeof(buffer) - 1)) {
        // Handle long data - simplified for this example
        value = std::string(buffer, sizeof(buffer) - 1);
    } else {
        value = std::string(buffer);
    }
    
    return value;
}

std::string ODBCResultSet::getString(const std::string& columnName) {
    size_t index = findColumn(columnName);
    return getString(index);
}

int ODBCResultSet::getInt(size_t columnIndex) {
    std::string value = getString(columnIndex);
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return 0;
    }
}

int ODBCResultSet::getInt(const std::string& columnName) {
    size_t index = findColumn(columnName);
    return getInt(index);
}

long long ODBCResultSet::getLong(size_t columnIndex) {
    std::string value = getString(columnIndex);
    try {
        return std::stoll(value);
    } catch (const std::exception&) {
        return 0;
    }
}

long long ODBCResultSet::getLong(const std::string& columnName) {
    size_t index = findColumn(columnName);
    return getLong(index);
}

double ODBCResultSet::getDouble(size_t columnIndex) {
    std::string value = getString(columnIndex);
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        return 0.0;
    }
}

double ODBCResultSet::getDouble(const std::string& columnName) {
    size_t index = findColumn(columnName);
    return getDouble(index);
}

bool ODBCResultSet::getBool(size_t columnIndex) {
    int value = getInt(columnIndex);
    return value != 0;
}

bool ODBCResultSet::getBool(const std::string& columnName) {
    size_t index = findColumn(columnName);
    return getBool(index);
}

size_t ODBCResultSet::getColumnCount() const {
    return columnNames_.size();
}

std::string ODBCResultSet::getColumnName(size_t columnIndex) const {
    if (columnIndex >= columnNames_.size()) {
        throw DatabaseException("Column index out of range");
    }
    return columnNames_[columnIndex];
}

size_t ODBCResultSet::findColumn(const std::string& columnName) const {
    for (size_t i = 0; i < columnNames_.size(); ++i) {
        if (columnNames_[i] == columnName) {
            return i;
        }
    }
    throw DatabaseException("Column not found: " + columnName);
}

void ODBCResultSet::close() {
    // Statement handle is managed by the parent statement
}

void ODBCResultSet::checkSQLReturn(SQLRETURN ret, const std::string& operation) {
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        throw DatabaseException(operation + ": " + getSQLErrorMessage(statement_, SQL_HANDLE_STMT));
    }
}

std::string ODBCResultSet::getSQLErrorMessage(SQLHANDLE handle, SQLSMALLINT handleType) {
    SQLCHAR sqlState[6];
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER nativeError;
    SQLSMALLINT messageLength;
    
    SQLGetDiagRecA(handleType, handle, 1, sqlState, &nativeError,
                  message, SQL_MAX_MESSAGE_LENGTH, &messageLength);

    return std::string(reinterpret_cast<char*>(message));
}

} // namespace Network::Database
