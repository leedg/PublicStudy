#pragma once

// Database utility functions

#include "IStatement.h"
#include <map>
#include <string>

namespace Network
{
namespace Database
{

// =============================================================================
// Database utility functions
// =============================================================================

/**
 * Utility functions
 */
namespace Utils
{
// Build connection strings
std::string
BuildODBCConnectionString(const std::map<std::string, std::string> &params);
std::string
BuildOLEDBConnectionString(const std::map<std::string, std::string> &params);

// Type-safe parameter binding helpers
template <typename T>
void BindParameterSafe(IStatement *pStmt, size_t index, const T &value);

// Specializations
template <>
void BindParameterSafe<std::string>(IStatement *pStmt, size_t index,
									const std::string &value);

template <>
void BindParameterSafe<int>(IStatement *pStmt, size_t index, const int &value);

template <>
void BindParameterSafe<long long>(IStatement *pStmt, size_t index,
								  const long long &value);

template <>
void BindParameterSafe<double>(IStatement *pStmt, size_t index,
								   const double &value);

template <>
void BindParameterSafe<bool>(IStatement *pStmt, size_t index,
							 const bool &value);
} // namespace Utils

} // namespace Database
} // namespace Network
