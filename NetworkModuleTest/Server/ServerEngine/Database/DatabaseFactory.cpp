// English: DatabaseFactory implementation
// 한글: DatabaseFactory 구현

#include "DatabaseFactory.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/DatabaseUtils.h"
#include "MockDatabase.h"
#ifdef _WIN32
#include "ODBCDatabase.h"
#include "OLEDBDatabase.h"
#endif
#include "PostgreSQLDatabase.h"
#include "SQLiteDatabase.h"
#include <map>
#include <stdexcept>

namespace Network
{
namespace Database
{

std::unique_ptr<IDatabase> DatabaseFactory::CreateDatabase(DatabaseType type)
{
	switch (type)
	{
#ifdef _WIN32
	case DatabaseType::ODBC:
	case DatabaseType::MySQL:
		return CreateODBCDatabase();
	case DatabaseType::OLEDB:
		return CreateOLEDBDatabase();
#endif
	case DatabaseType::PostgreSQL:
		return CreatePostgreSQLDatabase();
	case DatabaseType::Mock:
		return CreateMockDatabase();
	case DatabaseType::SQLite:
		return CreateSQLiteDatabase();
	default:
		throw DatabaseException("Unsupported database type");
	}
}

std::unique_ptr<IDatabase> DatabaseFactory::CreateODBCDatabase()
{
#ifdef _WIN32
	return std::make_unique<ODBCDatabase>();
#else
	throw DatabaseException("ODBC backend is only available on Windows");
#endif
}

std::unique_ptr<IDatabase> DatabaseFactory::CreateOLEDBDatabase()
{
#ifdef _WIN32
	return std::make_unique<OLEDBDatabase>();
#else
	throw DatabaseException("OLEDB backend is only available on Windows");
#endif
}

std::unique_ptr<IDatabase> DatabaseFactory::CreateMockDatabase()
{
	return std::make_unique<MockDatabase>();
}

std::unique_ptr<IDatabase> DatabaseFactory::CreateSQLiteDatabase()
{
	return std::make_unique<SQLiteDatabase>();
}

std::unique_ptr<IDatabase> DatabaseFactory::CreatePostgreSQLDatabase()
{
#ifdef HAVE_LIBPQ
	return std::make_unique<PostgreSQLDatabase>();
#else
	throw DatabaseException(
		"PostgreSQL (libpq) not available: recompile with HAVE_LIBPQ and link libpq");
#endif
}

namespace Utils
{

std::string
BuildODBCConnectionString(const std::map<std::string, std::string> &params)
{
	std::string connStr;
	bool first = true;

	for (const auto &param : params)
	{
		if (!first)
		{
			connStr += ";";
		}
		connStr += param.first + "=" + param.second;
		first = false;
	}

	return connStr;
}

std::string
BuildOLEDBConnectionString(const std::map<std::string, std::string> &params)
{
	std::string connStr;
	bool first = true;

	for (const auto &param : params)
	{
		if (!first)
		{
			connStr += ";";
		}
		connStr += param.first + "=" + param.second;
		first = false;
	}

	return connStr;
}

// English: Template specializations for parameter binding
// 한글: 파라미터 바인딩용 템플릿 특수화
template <>
void BindParameterSafe<std::string>(IStatement *pStmt, size_t index,
									const std::string &value)
{
	pStmt->BindParameter(index, value);
}

template <>
void BindParameterSafe<int>(IStatement *pStmt, size_t index, const int &value)
{
	pStmt->BindParameter(index, value);
}

template <>
void BindParameterSafe<long long>(IStatement *pStmt, size_t index,
								  const long long &value)
{
	pStmt->BindParameter(index, value);
}

template <>
void BindParameterSafe<double>(IStatement *pStmt, size_t index,
								   const double &value)
{
	pStmt->BindParameter(index, value);
}

template <>
void BindParameterSafe<bool>(IStatement *pStmt, size_t index, const bool &value)
{
	pStmt->BindParameter(index, value);
}

} // namespace Utils

} // namespace Database
} // namespace Network
