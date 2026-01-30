#pragma once

// English: ODBC database connection class
// 한글: ODBC 데이터베이스 연결 클래스

#include <string>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <sql.h>
    #include <sqlext.h>
    #pragma comment(lib, "odbc32.lib")
#endif

namespace Network::Database
{
    // =============================================================================
    // English: DBConnection class
    // 한글: DBConnection 클래스
    // =============================================================================

    class DBConnection
    {
    public:
        DBConnection();
        ~DBConnection();

        // English: Connect / Disconnect
        // 한글: 연결 / 해제
        bool Connect(const std::string& connectionString);
        void Disconnect();

        // English: Execute query
        // 한글: 쿼리 실행
        bool Execute(const std::string& query);

        // English: State
        // 한글: 상태
        bool IsConnected() const { return mConnected; }
        std::string GetLastError() const { return mLastError; }

    private:
#ifdef _WIN32
        SQLHENV     mEnv;
        SQLHDBC     mDbc;
        SQLHSTMT    mStmt;
#endif
        bool        mConnected;
        std::string mLastError;
    };

} // namespace Network::Database
