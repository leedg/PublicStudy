#pragma once

// Abstract connection interface

#include <memory>
#include <string>

namespace Network
{
namespace Database
{

// Forward declaration
class IStatement;

// =============================================================================
// IConnection interface
// =============================================================================

/**
 * Abstract connection interface
 */
class IConnection
{
  public:
	virtual ~IConnection() = default;

	// Connection management
	virtual void Open(const std::string &connectionString) = 0;
	virtual void Close() = 0;
	virtual bool IsOpen() const = 0;

	// Statement creation
	virtual std::unique_ptr<IStatement> CreateStatement() = 0;

	// Transaction management
	virtual void BeginTransaction() = 0;
	virtual void CommitTransaction() = 0;
	virtual void RollbackTransaction() = 0;

	// Error information
	virtual int GetLastErrorCode() const = 0;
	virtual std::string GetLastError() const = 0;
};

} // namespace Database
} // namespace Network
