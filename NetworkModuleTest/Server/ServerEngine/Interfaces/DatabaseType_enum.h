#pragma once

// English: Database type enumeration
// 한글: 데이터베이스 타입 열거형

namespace Network
{
namespace Database
{

// =============================================================================
// English: DatabaseType enumeration
// 한글: DatabaseType 열거형
// =============================================================================

/**
 * English: Database type enumeration
 * 한글: 데이터베이스 타입 열거형
 */
enum class DatabaseType
{
	// English: ODBC (Open Database Connectivity)
	// 한글: ODBC (개방형 데이터베이스 연결)
	ODBC,

	// English: OLE DB (Object Linking and Embedding Database)
	// 한글: OLE DB (객체 연결 및 포함 데이터베이스)
	OLEDB,

	// English: MySQL
	// 한글: MySQL
	MySQL,

	// English: PostgreSQL
	// 한글: PostgreSQL
	PostgreSQL,

	// English: SQLite
	// 한글: SQLite
	SQLite
};

} // namespace Database
} // namespace Network
