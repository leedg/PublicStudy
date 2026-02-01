#pragma once

#include "../Interfaces/IDatabase.h"
#include "../Interfaces/DatabaseType_enum.h"
#include <memory>

namespace Network::Database {

/**
 * Database factory for creating database instances
 */
class DatabaseFactory {
public:
    static std::unique_ptr<IDatabase> createDatabase(DatabaseType type);

    // Convenience methods
    static std::unique_ptr<IDatabase> createODBCDatabase();
    static std::unique_ptr<IDatabase> createOLEDBDatabase();
};

} // namespace Network::Database
