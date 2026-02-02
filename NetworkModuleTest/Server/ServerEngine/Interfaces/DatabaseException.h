#pragma once

// English: Exception class for database operations
// 한글: 데이터베이스 작업용 예외 클래스

#include <string>
#include <exception>

namespace Network {
namespace Database {

    // ==========================================================================
    // English: DatabaseException class
    // 한글: DatabaseException 클래스
    // ==========================================================================

    /**
     * English: Exception class for database operations
     * 한글: 데이터베이스 작업용 예외 클래스
     */
    class DatabaseException : public std::exception 
    {
    public:
        // English: Constructor
        // 한글: 생성자
        DatabaseException(const std::string& message, int errorCode = 0)
            : mMessage(message)
            , mErrorCode(errorCode) 
        {
        }

        // English: Get error message
        // 한글: 에러 메시지 조회
        const char* what() const noexcept override 
        {
            return mMessage.c_str();
        }

        // English: Get error code
        // 한글: 에러 코드 조회
        int GetErrorCode() const 
        { 
            return mErrorCode; 
        }

    private:
        std::string mMessage;
        int mErrorCode;
    };

}  // namespace Database
}  // namespace Network
