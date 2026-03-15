#pragma once

// Abstract result set interface

#include <string>

namespace Network
{
namespace Database
{

// =============================================================================
// IResultSet interface
// =============================================================================

/**
 * Abstract result set interface
 */
class IResultSet
{
  public:
	virtual ~IResultSet() = default;

	// Navigation
	virtual bool Next() = 0;

	// Null checking
	virtual bool IsNull(size_t columnIndex) = 0;
	virtual bool IsNull(const std::string &columnName) { return IsNull(FindColumn(columnName)); }

	// Data retrieval methods - by index
	virtual std::string GetString(size_t columnIndex) = 0;
	virtual int GetInt(size_t columnIndex) = 0;
	virtual long long GetLong(size_t columnIndex) = 0;
	virtual double GetDouble(size_t columnIndex) = 0;
	virtual bool GetBool(size_t columnIndex) = 0;

	// Data retrieval methods - by name (default: delegates to FindColumn + index variant)
	virtual std::string GetString(const std::string &columnName) { return GetString(FindColumn(columnName)); }
	virtual int GetInt(const std::string &columnName)            { return GetInt(FindColumn(columnName)); }
	virtual long long GetLong(const std::string &columnName)     { return GetLong(FindColumn(columnName)); }
	virtual double GetDouble(const std::string &columnName)      { return GetDouble(FindColumn(columnName)); }
	virtual bool GetBool(const std::string &columnName)          { return GetBool(FindColumn(columnName)); }

	// Metadata
	virtual size_t GetColumnCount() const = 0;
	virtual std::string GetColumnName(size_t columnIndex) const = 0;
	virtual size_t FindColumn(const std::string &columnName) const = 0;

	// Cleanup
	virtual void Close() = 0;
};

} // namespace Database
} // namespace Network
