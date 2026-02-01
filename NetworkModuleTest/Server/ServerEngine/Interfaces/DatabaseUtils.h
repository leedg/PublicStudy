#pragma once

#include "IStatement.h"
#include <string>
#include <map>

namespace Network::Database {

/**
 * Utility functions
 */
namespace Utils {
    std::string buildODBCConnectionString(const std::map<std::string, std::string>& params);
    std::string buildOLEDBConnectionString(const std::map<std::string, std::string>& params);

    // Type-safe parameter binding helpers
    template<typename T>
    void bindParameterSafe(IStatement* stmt, size_t index, const T& value);

    // Specializations
    template<>
    void bindParameterSafe<std::string>(IStatement* stmt, size_t index, const std::string& value);
    template<>
    void bindParameterSafe<int>(IStatement* stmt, size_t index, const int& value);
    template<>
    void bindParameterSafe<long long>(IStatement* stmt, size_t index, const long long& value);
    template<>
    void bindParameterSafe<double>(IStatement* stmt, size_t index, const double& value);
    template<>
    void bindParameterSafe<bool>(IStatement* stmt, size_t index, const bool& value);
}

} // namespace Network::Database
