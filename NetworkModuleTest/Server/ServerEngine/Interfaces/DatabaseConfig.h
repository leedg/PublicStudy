#pragma once

#include "DatabaseType_enum.h"
#include <string>

namespace Network::Database {

/**
 * Database configuration structure
 */
struct DatabaseConfig {
    std::string connectionString;
    DatabaseType type = DatabaseType::ODBC;
    int connectionTimeout = 30;
    int commandTimeout = 30;
    bool autoCommit = true;
    int maxPoolSize = 10;
    int minPoolSize = 2;
};

} // namespace Network::Database
