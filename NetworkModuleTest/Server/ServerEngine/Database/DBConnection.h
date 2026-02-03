#pragma once

// English: ODBC database connection class
// ???: ODBC ?怨쀬뵠?怨뺤퓢??곷뮞 ?怨뚭퍙 ?????

#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
// 한글: ODBC 헤더가 필요로 하는 Windows 타입을 먼저 정의한다.
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#pragma comment(lib, "odbc32.lib")
#endif

namespace Network::Database
{
// =============================================================================
// English: DBConnection class
// ???: DBConnection ?????
// =============================================================================

class DBConnection
{
  public:
	DBConnection();
	~DBConnection();

	// English: Connect / Disconnect
	// ???: ?怨뚭퍙 / ??곸젫
	bool Connect(const std::string &connectionString);
	void Disconnect();

	// English: Execute query
	// ???: ?묒눖????쎈뻬
	bool Execute(const std::string &query);

	// English: State
	// ???: ?怨밴묶
	bool IsConnected() const { return mConnected; }
	std::string GetLastError() const { return mLastError; }

  private:
#ifdef _WIN32
	SQLHENV mEnv;
	SQLHDBC mDbc;
	SQLHSTMT mStmt;
#endif
	bool mConnected;
	std::string mLastError;
};

} // namespace Network::Database
