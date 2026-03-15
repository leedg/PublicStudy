#pragma once

// Database configuration structure

#include "DatabaseType_enum.h"
#include <string>
#include <sstream>

namespace Network
{
namespace Database
{

// =============================================================================
// DatabaseConfig structure
// =============================================================================

/**
 * Database configuration structure
 */
struct DatabaseConfig
{
	// Connection string
	std::string mConnectionString;

	// Database type
	DatabaseType mType = DatabaseType::ODBC;

	// Connection timeout in seconds
	int mConnectionTimeout = 30;

	// Command timeout in seconds
	int mCommandTimeout = 30;

	// Auto-commit mode
	bool mAutoCommit = true;

	// Maximum pool size
	int mMaxPoolSize = 10;

	// Minimum pool size
	int mMinPoolSize = 2;

	// Server host / port for connection string helpers (below).
	std::string mHost     = "localhost";
	uint16_t    mPort     = 1433;   // SQL Server default
	std::string mDatabase;
	std::string mUser;
	std::string mPassword;

	// ─── Connection string helpers ───────────────────────────────────────────
	// Example ODBC (SQL Server):
	//   Driver={ODBC Driver 17 for SQL Server};Server=localhost,1433;
	//   Database=mydb;UID=sa;PWD=secret;
	// Example ODBC (PostgreSQL):
	//   Driver={PostgreSQL Unicode};Server=localhost;Port=5432;
	//   Database=mydb;UID=postgres;PWD=secret;
	std::string BuildODBCConnectionString() const
	{
		std::ostringstream ss;
		ss << "Server=" << mHost << "," << mPort << ";"
		   << "Database=" << mDatabase << ";"
		   << "UID=" << mUser << ";"
		   << "PWD=" << mPassword << ";";
		return ss.str();
	}

	// Example OLEDB (SQL Server SQLOLEDB provider):
	//   Provider=SQLOLEDB;Data Source=localhost,1433;
	//   Initial Catalog=mydb;User Id=sa;Password=secret;
	std::string BuildOLEDBConnectionString() const
	{
		std::ostringstream ss;
		ss << "Provider=SQLOLEDB;"
		   << "Data Source=" << mHost << "," << mPort << ";"
		   << "Initial Catalog=" << mDatabase << ";"
		   << "User Id=" << mUser << ";"
		   << "Password=" << mPassword << ";";
		return ss.str();
	}
};

} // namespace Database
} // namespace Network
