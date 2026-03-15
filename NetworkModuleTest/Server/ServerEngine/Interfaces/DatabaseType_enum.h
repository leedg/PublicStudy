#pragma once

// Database type enumeration

namespace Network
{
namespace Database
{

// =============================================================================
// DatabaseType enumeration
// =============================================================================

/**
 * Database type enumeration
 */
enum class DatabaseType
{
	// ODBC (Open Database Connectivity)
	ODBC,

	// OLE DB (Object Linking and Embedding Database)
	OLEDB,

	// MySQL
	MySQL,

	// PostgreSQL
	PostgreSQL,

	// SQLite
	SQLite,

	// Mock (in-memory, no external dependency, for testing)
	Mock
};

} // namespace Database
} // namespace Network
