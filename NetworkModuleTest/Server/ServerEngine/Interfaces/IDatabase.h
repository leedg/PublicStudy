#pragma once

// Abstract database interface

#include "DatabaseConfig.h"
#include "DatabaseType_enum.h"
#include <memory>

namespace Network
{
namespace Database
{

// Forward declarations
class IConnection;
class IStatement;

// =============================================================================
// IDatabase interface
// =============================================================================

/**
 * Abstract database interface
 */
class IDatabase
{
  public:
	virtual ~IDatabase() = default;

	// Connection management
	virtual void Connect(const DatabaseConfig &config) = 0;
	virtual void Disconnect() = 0;
	virtual bool IsConnected() const = 0;

	// Object creation
	virtual std::unique_ptr<IConnection> CreateConnection() = 0;
	virtual std::unique_ptr<IStatement> CreateStatement() = 0;

	// Database-level transaction management.
	//          NOTE: Transaction state is per-connection in most databases.
	//          Prefer using IConnection::BeginTransaction() on a connection obtained
	//          from CreateConnection() or ConnectionPool::GetConnection().
	//          Implementations may throw DatabaseException for this reason.
	virtual void BeginTransaction() = 0;
	virtual void CommitTransaction() = 0;
	virtual void RollbackTransaction() = 0;

	// Information
	virtual DatabaseType GetType() const = 0;
	virtual const DatabaseConfig &GetConfig() const = 0;
};

} // namespace Database
} // namespace Network
