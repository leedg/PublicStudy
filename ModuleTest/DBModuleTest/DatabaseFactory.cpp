#include "DatabaseFactory.h"
#include "ODBCDatabase.h"
#include "OLEDBDatabase.h"
#include <stdexcept>

namespace DocDBModule
{

std::unique_ptr<IDatabase> DatabaseFactory::createDatabase(DatabaseType type)
{
	switch (type)
	{
	case DatabaseType::ODBC:
		return createODBCDatabase();
	case DatabaseType::OLEDB:
		return createOLEDBDatabase();
	default:
		throw DatabaseException("Unsupported database type");
	}
}

std::unique_ptr<IDatabase> DatabaseFactory::createODBCDatabase()
{
	return std::make_unique<ODBCDatabase>();
}

std::unique_ptr<IDatabase> DatabaseFactory::createOLEDBDatabase()
{
	return std::make_unique<OLEDBDatabase>();
}

namespace Utils
{

std::string
buildODBCConnectionString(const std::map<std::string, std::string> &params)
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
buildOLEDBConnectionString(const std::map<std::string, std::string> &params)
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

// Template specializations for parameter binding
template <>
void bindParameterSafe<std::string>(IStatement *stmt, size_t index,
									const std::string &value)
{
	stmt->bindParameter(index, value);
}

template <>
void bindParameterSafe<int>(IStatement *stmt, size_t index, const int &value)
{
	stmt->bindParameter(index, value);
}

template <>
void bindParameterSafe<long long>(IStatement *stmt, size_t index,
								  const long long &value)
{
	stmt->bindParameter(index, value);
}

template <>
void bindParameterSafe<double>(IStatement *stmt, size_t index,
								   const double &value)
{
	stmt->bindParameter(index, value);
}

template <>
void bindParameterSafe<bool>(IStatement *stmt, size_t index, const bool &value)
{
	stmt->bindParameter(index, value);
}

} // namespace Utils

} // namespace DocDBModule
