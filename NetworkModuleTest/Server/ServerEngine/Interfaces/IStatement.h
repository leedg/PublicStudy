#pragma once

// Abstract statement interface

#include <memory>
#include <string>
#include <vector>

namespace Network
{
namespace Database
{

// Forward declaration
class IResultSet;

// =============================================================================
// IStatement interface
// =============================================================================

/**
 * Abstract statement interface
 */
class IStatement
{
  public:
	virtual ~IStatement() = default;

	// Query configuration
	virtual void SetQuery(const std::string &query) = 0;
	virtual void SetTimeout(int seconds) = 0;

	// Parameter binding — index is 1-based (first parameter = 1).
	virtual void BindParameter(size_t index, const std::string &value) = 0;
	virtual void BindParameter(size_t index, int value) = 0;
	virtual void BindParameter(size_t index, long long value) = 0;
	virtual void BindParameter(size_t index, double value) = 0;
	virtual void BindParameter(size_t index, bool value) = 0;
	virtual void BindNullParameter(size_t index) = 0;

	// Query execution
	virtual std::unique_ptr<IResultSet> ExecuteQuery() = 0;
	virtual int ExecuteUpdate() = 0;
	virtual bool Execute() = 0;

	// Batch operations
	virtual void AddBatch() = 0;
	virtual std::vector<int> ExecuteBatch() = 0;

	// Cleanup
	virtual void ClearParameters() = 0;
	virtual void Close() = 0;
};

} // namespace Database
} // namespace Network
