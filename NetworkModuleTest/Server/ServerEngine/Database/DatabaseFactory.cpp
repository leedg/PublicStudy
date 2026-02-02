// English: DatabaseFactory implementation
// 한글: DatabaseFactory 구현

#include "DatabaseFactory.h"
#include "ODBCDatabase.h"
#include "OLEDBDatabase.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/DatabaseUtils.h"
#include <stdexcept>
#include <map>

namespace Network {
namespace Database {

    std::unique_ptr<IDatabase> DatabaseFactory::CreateDatabase(DatabaseType type) 
    {
        switch (type) 
        {
        case DatabaseType::ODBC:
            return CreateODBCDatabase();
        case DatabaseType::OLEDB:
            return CreateOLEDBDatabase();
        default:
            throw DatabaseException("Unsupported database type");
        }
    }

    std::unique_ptr<IDatabase> DatabaseFactory::CreateODBCDatabase() 
    {
        return std::make_unique<ODBCDatabase>();
    }

    std::unique_ptr<IDatabase> DatabaseFactory::CreateOLEDBDatabase() 
    {
        return std::make_unique<OLEDBDatabase>();
    }

    namespace Utils {

        std::string BuildODBCConnectionString(const std::map<std::string, std::string>& params) 
        {
            std::string connStr;
            bool first = true;

            for (const auto& param : params) 
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

        std::string BuildOLEDBConnectionString(const std::map<std::string, std::string>& params) 
        {
            std::string connStr;
            bool first = true;

            for (const auto& param : params) 
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
        template<>
        void BindParameterSafe<std::string>(IStatement* pStmt, size_t index, const std::string& value) 
        {
            pStmt->BindParameter(index, value);
        }

        template<>
        void BindParameterSafe<int>(IStatement* pStmt, size_t index, const int& value) 
        {
            pStmt->BindParameter(index, value);
        }

        template<>
        void BindParameterSafe<long long>(IStatement* pStmt, size_t index, const long long& value) 
        {
            pStmt->BindParameter(index, value);
        }

        template<>
        void BindParameterSafe<double>(IStatement* pStmt, size_t index, const double& value) 
        {
            pStmt->BindParameter(index, value);
        }

        template<>
        void BindParameterSafe<bool>(IStatement* pStmt, size_t index, const bool& value) 
        {
            pStmt->BindParameter(index, value);
        }

    }  // namespace Utils

}  // namespace Database
}  // namespace Network
