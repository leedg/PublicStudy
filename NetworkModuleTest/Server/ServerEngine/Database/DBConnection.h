#pragma once

// English: ODBC database connection class
// ?쒓?: ODBC ?곗씠?곕쿋?댁뒪 ?곌껐 ?대옒??

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
    // ?쒓?: DBConnection ?대옒??
    // =============================================================================

    class DBConnection
    {
    public:
        DBConnection();
        ~DBConnection();

        // English: Connect / Disconnect
        // ?쒓?: ?곌껐 / ?댁젣
        bool Connect(const std::string& connectionString);
        void Disconnect();

        // English: Execute query
        // ?쒓?: 荑쇰━ ?ㅽ뻾
        bool Execute(const std::string& query);

        // English: State
        // ?쒓?: ?곹깭
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

