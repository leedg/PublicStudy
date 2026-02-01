#pragma once

/**
 * Database Module - Unified Database Access Layer
 *
 * This module provides a unified interface for database operations across different
 * database systems (ODBC, OLEDB, MySQL, PostgreSQL, SQLite).
 *
 * Features:
 * - Abstract database interface (IDatabase, IConnection, IStatement, IResultSet)
 * - Connection pooling with automatic connection management
 * - ODBC and OLEDB implementations
 * - Thread-safe connection pool
 * - RAII-based resource management
 * - Exception-based error handling
 *
 * Usage Example:
 *
 * // Create database and connection pool
 * DatabaseConfig config;
 * config.type = DatabaseType::ODBC;
 * config.connectionString = "DSN=MyDatabase;UID=user;PWD=pass";
 * config.maxPoolSize = 10;
 * config.minPoolSize = 2;
 *
 * ConnectionPool pool;
 * if (!pool.initialize(config)) {
 *     // Handle error
 * }
 *
 * // Get connection from pool
 * auto conn = pool.getConnection();
 * auto stmt = conn->createStatement();
 * stmt->setQuery("SELECT * FROM users WHERE id = ?");
 * stmt->bindParameter(1, userId);
 *
 * auto rs = stmt->executeQuery();
 * while (rs->next()) {
 *     std::string name = rs->getString("name");
 *     int age = rs->getInt("age");
 * }
 *
 * pool.returnConnection(conn);
 *
 * // Or use RAII wrapper
 * {
 *     ScopedConnection scopedConn(pool.getConnection(), &pool);
 *     auto stmt = scopedConn->createStatement();
 *     // ... use statement
 *     // Connection automatically returned when scope ends
 * }
 */

// Core interfaces
#include "../Interfaces/DatabaseType_enum.h"
#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IDatabase.h"
#include "../Interfaces/IConnection.h"
#include "../Interfaces/IStatement.h"
#include "../Interfaces/IResultSet.h"
#include "../Interfaces/IConnectionPool.h"
#include "../Interfaces/DatabaseUtils.h"

// Database implementations
#include "ODBCDatabase.h"
#include "OLEDBDatabase.h"

// Factory and utilities
#include "DatabaseFactory.h"
#include "ConnectionPool.h"

// Legacy support (deprecated)
#include "DBConnection.h"
#include "DBConnectionPool.h"

namespace Network::Database {

/**
 * Module version information
 */
struct ModuleVersion {
    static constexpr int MAJOR = 1;
    static constexpr int MINOR = 0;
    static constexpr int PATCH = 0;

    static constexpr const char* VERSION_STRING = "1.0.0";
    static constexpr const char* BUILD_DATE = __DATE__;
};

/**
 * Helper function to create a configured connection pool
 */
inline std::unique_ptr<ConnectionPool> createConnectionPool(const DatabaseConfig& config) {
    auto pool = std::make_unique<ConnectionPool>();
    if (!pool->initialize(config)) {
        return nullptr;
    }
    return pool;
}

/**
 * Helper function to create a database instance
 */
inline std::unique_ptr<IDatabase> createDatabase(DatabaseType type, const DatabaseConfig& config) {
    auto db = DatabaseFactory::createDatabase(type);
    if (db) {
        db->connect(config);
    }
    return db;
}

} // namespace Network::Database
