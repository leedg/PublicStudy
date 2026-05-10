#pragma once

// 팩토리 패턴 적용 이유:
//   DatabaseType enum 값 하나로 모든 백엔드의 생성 로직을 캡슐화한다.
//   호출부에서 ODBCDatabase/SQLiteDatabase 등 구체 타입을 직접 포함하지 않으므로
//   플랫폼 조건부 헤더(windows.h, sql.h 등)가 호출부로 누출되지 않는다.
//   또한 백엔드를 교체하거나 추가할 때 팩토리 구현만 수정하면 된다.

#include "../Interfaces/DatabaseType_enum.h"
#include "../Interfaces/IDatabase.h"
#include <memory>

namespace Network
{
namespace Database
{

// =============================================================================
// DatabaseFactory — DatabaseType별 IDatabase 인스턴스 생성
// =============================================================================

class DatabaseFactory
{
  public:
	// type에 따라 적절한 IDatabase 구현을 생성하여 반환.
	// 플랫폼에서 지원하지 않는 type이거나 빌드 조건이 맞지 않으면 DatabaseException.
	// (ODBC/OLEDB는 Windows 전용, PostgreSQL은 HAVE_LIBPQ 필요,
	//  SQLite는 HAVE_SQLITE3 필요 — 단 SQLite는 스텁이 있어 Connect()까지 예외가 지연됨)
	static std::unique_ptr<IDatabase> CreateDatabase(DatabaseType type);

	// 편의 메서드 — 타입별 직접 생성
	static std::unique_ptr<IDatabase> CreateODBCDatabase();
	static std::unique_ptr<IDatabase> CreateOLEDBDatabase();
	static std::unique_ptr<IDatabase> CreateMockDatabase();
	static std::unique_ptr<IDatabase> CreateSQLiteDatabase();
	static std::unique_ptr<IDatabase> CreatePostgreSQLDatabase();
};

} // namespace Database
} // namespace Network
