#pragma once

// Database factory for creating database instances

#include "../Interfaces/DatabaseType_enum.h"
#include "../Interfaces/IDatabase.h"
#include <memory>

namespace Network
{
namespace Database
{

// =============================================================================
// DatabaseFactory class
// =============================================================================

/**
 * Database factory for creating database instances
 */
class DatabaseFactory
{
  public:
	// Create database by type
	static std::unique_ptr<IDatabase> CreateDatabase(DatabaseType type);

	// Convenience methods
	static std::unique_ptr<IDatabase> CreateODBCDatabase();
	static std::unique_ptr<IDatabase> CreateOLEDBDatabase();
	static std::unique_ptr<IDatabase> CreateMockDatabase();
	static std::unique_ptr<IDatabase> CreateSQLiteDatabase();
};

} // namespace Database
} // namespace Network
