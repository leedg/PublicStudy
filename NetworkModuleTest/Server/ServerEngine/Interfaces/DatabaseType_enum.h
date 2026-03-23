#pragma once

// 지원하는 데이터베이스 백엔드 타입 열거형.
// DatabaseFactory::CreateDatabase()의 분기 키로 사용된다.
// 플랫폼 제약에 유의: ODBC/OLEDB는 Windows 전용 헤더에 의존한다.

namespace Network
{
namespace Database
{

// =============================================================================
// DatabaseType 열거형
// =============================================================================

enum class DatabaseType
{
	// ODBC (Open Database Connectivity).
	// Windows 전용 (sql.h / sqlext.h 필요).
	// MySQL, SQL Server, PostgreSQL 등 DSN 설정이 있는 모든 DB에 범용으로 사용 가능.
	// DatabaseFactory에서 MySQL 타입도 이 백엔드로 라우팅된다.
	ODBC,

	// OLE DB (Object Linking and Embedding Database).
	// Windows 전용 (oledb.h / SQLOLEDB 공급자 필요).
	// ODBC보다 낮은 레이어에서 동작하며 SQL Server에 특화된 기능(서버 커서 등)을 지원.
	// COM 초기화(CoInitializeEx)가 필요하므로 IOCP 환경에서는 COINIT_MULTITHREADED 사용.
	OLEDB,

	// MySQL / MariaDB.
	// 현재 이 프로젝트에서는 MySQL 전용 네이티브 클라이언트가 아닌
	// ODBC 백엔드(DatabaseType::ODBC)로 라우팅된다.
	MySQL,

	// PostgreSQL.
	// libpq를 직접 사용 (ODBC 경유 없음).
	// ODBC 드라이버 설치 없이 크로스 플랫폼 지원이 가능하며,
	// PQexecParams를 통한 바이너리 파라미터 바인딩으로 SQL 인젝션을 차단한다.
	// 빌드 시 HAVE_LIBPQ 매크로와 libpq 링크가 필요.
	PostgreSQL,

	// SQLite (파일 기반 / 인메모리).
	// 외부 서버 없이 단일 파일로 동작하므로 로컬 캐시나 설정 저장에 적합.
	// 빌드 시 HAVE_SQLITE3 매크로와 sqlite3 링크가 필요.
	// HAVE_SQLITE3 없이 사용하면 Connect() 호출 시 DatabaseException 발생.
	SQLite,

	// Mock (인메모리, 외부 의존성 없음).
	// 테스트에서 실제 DB 없이 쿼리 실행 흐름을 검증할 때 사용.
	// 서버 시작 시 DB 연결 없이 스키마 부트스트랩(SqlModuleBootstrap)을 통과시키는
	// 용도로도 활용된다 (DatabaseType::Mock이면 bootstrap 상태 테이블 검사를 우회).
	Mock
};

} // namespace Database
} // namespace Network
