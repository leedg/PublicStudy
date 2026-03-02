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
#include "SQLiteDatabase.h"
#include <iostream>
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
		return CreateODBCDatabase();
#endif
	case DatabaseType::OLEDB:
		return CreateOLEDBDatabase();
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
	std::cerr << "[DatabaseFactory] ODBC backend is only available on Windows. "
	             "Returning nullptr — use SQLite or Mock instead.\n";
	return nullptr;
#endif
}

std::unique_ptr<IDatabase> DatabaseFactory::CreateOLEDBDatabase()
{
#ifdef _WIN32
	return std::make_unique<OLEDBDatabase>();
#else
	std::cerr << "[DatabaseFactory] OLEDB backend is only available on Windows. "
	             "Returning nullptr — use ODBC or SQLite instead.\n";
	return nullptr;
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
