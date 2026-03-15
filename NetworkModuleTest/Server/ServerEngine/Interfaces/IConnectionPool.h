#pragma once

// Connection pool interface

#include "IConnection.h"
#include <memory>

namespace Network
{
namespace Database
{

// =============================================================================
// IConnectionPool interface
// =============================================================================

/**
 * Connection pool interface
 */
class IConnectionPool
{
  public:
	virtual ~IConnectionPool() = default;

	// Get a connection from the pool. Blocks up to the configured connection
	//          timeout. Returns nullptr if the timeout expires before a connection
	//          becomes available (pool exhausted). Always check the return value before use,
	//          or wrap in ScopedConnection and call IsValid().
	virtual std::shared_ptr<IConnection> GetConnection() = 0;

	// Return a connection to the pool
	virtual void ReturnConnection(std::shared_ptr<IConnection> pConnection) = 0;

	// Clear all connections
	virtual void Clear() = 0;

	// Get number of active connections
	virtual size_t GetActiveConnections() const = 0;

	// Get number of available connections
	virtual size_t GetAvailableConnections() const = 0;
};

} // namespace Database
} // namespace Network
