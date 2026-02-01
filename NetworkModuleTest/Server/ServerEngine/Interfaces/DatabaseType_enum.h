#pragma once

namespace Network::Database {

/**
 * Database type enumeration
 */
enum class DatabaseType {
    ODBC,
    OLEDB,
    MySQL,
    PostgreSQL,
    SQLite
};

} // namespace Network::Database
