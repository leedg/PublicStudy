#pragma once

#include <string>
#include <exception>

namespace Network::Database {

/**
 * Exception class for database operations
 */
class DatabaseException : public std::exception {
private:
    std::string message_;
    int errorCode_;

public:
    DatabaseException(const std::string& message, int errorCode = 0)
        : message_(message), errorCode_(errorCode) {}

    const char* what() const noexcept override {
        return message_.c_str();
    }

    int getErrorCode() const { return errorCode_; }
};

} // namespace Network::Database
