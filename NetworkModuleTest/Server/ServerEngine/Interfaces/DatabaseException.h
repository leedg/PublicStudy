#pragma once

// Exception class for database operations

#include <exception>
#include <string>

namespace Network
{
namespace Database
{

// ==========================================================================
// DatabaseException class
// ==========================================================================

/**
 * Exception class for database operations
 */
class DatabaseException : public std::exception
{
  public:
	// Constructor
	DatabaseException(const std::string &message, int errorCode = 0)
		: mMessage(message), mErrorCode(errorCode)
	{
	}

	// Get error message
	const char *what() const noexcept override { return mMessage.c_str(); }

	// Get error code
	int GetErrorCode() const { return mErrorCode; }

  private:
	std::string mMessage;
	int mErrorCode;
};

} // namespace Database
} // namespace Network
