#pragma once

#include "IDatabase.h"
#include <memory>

namespace DocDBModule {

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

} // namespace DocDBModule
