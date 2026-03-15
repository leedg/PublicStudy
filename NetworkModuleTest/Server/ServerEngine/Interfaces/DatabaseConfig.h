#pragma once

// English: Database configuration structure
// 한글: 데이터베이스 설정 구조체

#include "DatabaseType_enum.h"
#include <string>
#include <sstream>

namespace Network
{
namespace Database
{

// =============================================================================
// English: DatabaseConfig structure
// 한글: DatabaseConfig 구조체
// =============================================================================

/**
 * English: Database configuration structure
 * 한글: 데이터베이스 설정 구조체
 */
struct DatabaseConfig
{
	// English: Connection string
	// 한글: 연결 문자열
	std::string mConnectionString;

	// English: Database type
	// 한글: 데이터베이스 타입
	DatabaseType mType = DatabaseType::ODBC;

	// English: Connection timeout in seconds
	// 한글: 연결 타임아웃 (초)
	int mConnectionTimeout = 30;

	// English: Command timeout in seconds
	// 한글: 명령 타임아웃 (초)
	int mCommandTimeout = 30;

	// English: Auto-commit mode
	// 한글: 자동 커밋 모드
	bool mAutoCommit = true;

	// English: Maximum pool size
	// 한글: 최대 풀 크기
	int mMaxPoolSize = 10;

	// English: Minimum pool size
	// 한글: 최소 풀 크기
	int mMinPoolSize = 2;

	// English: Server host / port for connection string helpers (below).
	// 한글: 연결 문자열 헬퍼용 서버 호스트 / 포트.
	std::string mHost     = "localhost";
	uint16_t    mPort     = 1433;   // SQL Server default
	std::string mDatabase;
	std::string mUser;
	std::string mPassword;

	// ─── Connection string helpers ───────────────────────────────────────────
	// Example ODBC (SQL Server):
	//   Driver={ODBC Driver 17 for SQL Server};Server=localhost,1433;
	//   Database=mydb;UID=sa;PWD=secret;
	// Example ODBC (PostgreSQL):
	//   Driver={PostgreSQL Unicode};Server=localhost;Port=5432;
	//   Database=mydb;UID=postgres;PWD=secret;
	std::string BuildODBCConnectionString() const
	{
		std::ostringstream ss;
		ss << "Server=" << mHost << "," << mPort << ";"
		   << "Database=" << mDatabase << ";"
		   << "UID=" << mUser << ";"
		   << "PWD=" << mPassword << ";";
		return ss.str();
	}

	// Example OLEDB (SQL Server SQLOLEDB provider):
	//   Provider=SQLOLEDB;Data Source=localhost,1433;
	//   Initial Catalog=mydb;User Id=sa;Password=secret;
	std::string BuildOLEDBConnectionString() const
	{
		std::ostringstream ss;
		ss << "Provider=SQLOLEDB;"
		   << "Data Source=" << mHost << "," << mPort << ";"
		   << "Initial Catalog=" << mDatabase << ";"
		   << "User Id=" << mUser << ";"
		   << "Password=" << mPassword << ";";
		return ss.str();
	}
};

} // namespace Database
} // namespace Network
