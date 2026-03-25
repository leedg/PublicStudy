#pragma once

// 데이터베이스 설정 구조체 및 SQL dialect 열거형.

#include "DatabaseType_enum.h"
#include <string>
#include <sstream>
#ifndef _WIN32
#include <cstdint>
#endif

namespace Network
{
namespace Database
{

// =============================================================================
// SQL dialect 열거형 — 스크립트 방언 선택 키
// =============================================================================

enum class SqlDialect
{
	Auto,       // DatabaseConfig에서 자동 감지
	Generic,    // 방언 무관 범용 SQL
	SQLite,     // SQLite 방언
	MySQL,      // MySQL / MariaDB 방언
	PostgreSQL, // PostgreSQL 방언
	SQLServer   // SQL Server (ODBC/OLEDB) 방언
};

// =============================================================================
// DatabaseConfig — 백엔드 연결 설정 구조체
// =============================================================================

struct DatabaseConfig
{
	std::string mConnectionString;        // 드라이버/공급자 연결 문자열; 비어 있으면 mHost 등 필드로 조합

	DatabaseType mType = DatabaseType::ODBC;  // 사용할 DB 백엔드 타입

	// ODBC/OLEDB DSN 경유처럼 백엔드 타입만으로 방언을 특정할 수 없을 때의 힌트.
	// Auto이면 mType 및 mConnectionString에서 자동 감지한다.
	SqlDialect mSqlDialectHint = SqlDialect::Auto;

	int  mConnectionTimeout = 30;   // GetConnection() / 드라이버 연결 최대 대기 시간 (초)
	int  mCommandTimeout    = 30;   // 쿼리 실행 제한 시간 (초); 0이면 타임아웃 없음
	bool mAutoCommit        = true; // true이면 각 쿼리를 자동 커밋

	int mMaxPoolSize = 10;  // ConnectionPool 동시 active 연결 상한
	int mMinPoolSize = 2;   // ConnectionPool 초기 미리 생성 연결 수 (워밍업)

	// 연결 문자열 헬퍼용 개별 필드 — BuildODBCConnectionString() 등에서 조합
	std::string mHost     = "localhost";  // DB 서버 호스트명 또는 IP
	uint16_t    mPort     = 1433;         // 포트 번호 (기본값: SQL Server 1433)
	std::string mDatabase;                // 데이터베이스(스키마) 이름
	std::string mUser;                    // 접속 사용자 이름
	std::string mPassword;                // 접속 비밀번호 (평문 — 프로덕션 환경에서는 별도 관리)

	// ─── Connection string helpers ───────────────────────────────────────────
	// Example ODBC (SQL Server):
	//   Driver={ODBC Driver 17 for SQL Server};Server=localhost,1433;
	//   Database=mydb;UID=sa;PWD=secret;
	// Example ODBC (PostgreSQL):
	//   Driver={PostgreSQL Unicode};Server=localhost;Port=5432;
	//   Database=mydb;UID=postgres;PWD=secret;
	SqlDialect ResolveSqlDialect() const
	{
		if (mSqlDialectHint != SqlDialect::Auto)
		{
			return mSqlDialectHint;
		}

		switch (mType)
		{
		case DatabaseType::SQLite:
			return SqlDialect::SQLite;

		case DatabaseType::MySQL:
			return SqlDialect::MySQL;

		case DatabaseType::PostgreSQL:
			return SqlDialect::PostgreSQL;

		case DatabaseType::OLEDB:
			return SqlDialect::SQLServer;

		default:
			return SqlDialect::Generic;
		}
	}

	std::string BuildODBCConnectionString() const
	{
		std::ostringstream ss;
		switch (ResolveSqlDialect())
		{
		case SqlDialect::MySQL:
		case SqlDialect::PostgreSQL:
			ss << "Server=" << mHost << ";"
			   << "Port=" << mPort << ";"
			   << "Database=" << mDatabase << ";"
			   << "UID=" << mUser << ";"
			   << "PWD=" << mPassword << ";";
			break;

		case SqlDialect::SQLite:
			ss << "Database=" << mDatabase << ";";
			break;

		default:
			ss << "Server=" << mHost << "," << mPort << ";"
			   << "Database=" << mDatabase << ";"
			   << "UID=" << mUser << ";"
			   << "PWD=" << mPassword << ";";
			break;
		}
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
