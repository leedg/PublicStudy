// English: OLEDBDatabase implementation
// 한글: OLEDBDatabase 구현

#include "OLEDBDatabase.h"
#include <stdexcept>

namespace Network {
namespace Database {

    // =============================================================================
    // English: OLEDBDatabase Implementation
    // 한글: OLEDBDatabase 구현
    // =============================================================================

    OLEDBDatabase::OLEDBDatabase() 
        : mConnected(false)
    {
    }

    OLEDBDatabase::~OLEDBDatabase()
    {
        Disconnect();
    }

    void OLEDBDatabase::Connect(const DatabaseConfig& config)
    {
        mConfig = config;
        mConnected = true;
    }

    void OLEDBDatabase::Disconnect()
    {
        mConnected = false;
    }

    bool OLEDBDatabase::IsConnected() const
    {
        return mConnected;
    }

    std::unique_ptr<IConnection> OLEDBDatabase::CreateConnection()
    {
        if (!mConnected)
        {
            throw DatabaseException("Database not connected");
        }
        return std::make_unique<OLEDBConnection>();
    }

    std::unique_ptr<IStatement> OLEDBDatabase::CreateStatement()
    {
        if (!mConnected)
        {
            throw DatabaseException("Database not connected");
        }
        return std::make_unique<OLEDBStatement>();
    }

    void OLEDBDatabase::BeginTransaction()
    {
        // English: OLEDB transaction implementation
        // 한글: OLEDB 트랜잭션 구현
    }

    void OLEDBDatabase::CommitTransaction()
    {
        // English: OLEDB commit implementation
        // 한글: OLEDB 커밋 구현
    }

    void OLEDBDatabase::RollbackTransaction()
    {
        // English: OLEDB rollback implementation
        // 한글: OLEDB 롤백 구현
    }

    // =============================================================================
    // English: OLEDBConnection Implementation
    // 한글: OLEDBConnection 구현
    // =============================================================================

    OLEDBConnection::OLEDBConnection() 
        : mConnected(false)
        , mLastErrorCode(0)
    {
    }

    OLEDBConnection::~OLEDBConnection()
    {
        Close();
    }

    void OLEDBConnection::Open([[maybe_unused]] const std::string& connectionString)
    {
        if (mConnected)
        {
            return; // English: Already connected / 한글: 이미 연결됨
        }

        // English: OLEDB connection implementation
        // 한글: OLEDB 연결 구현
        mConnected = true;
    }

    void OLEDBConnection::Close()
    {
        mConnected = false;
    }

    bool OLEDBConnection::IsOpen() const
    {
        return mConnected;
    }

    std::unique_ptr<IStatement> OLEDBConnection::CreateStatement()
    {
        if (!mConnected)
        {
            throw DatabaseException("Connection not open");
        }
        return std::make_unique<OLEDBStatement>();
    }

    void OLEDBConnection::BeginTransaction()
    {
        // English: OLEDB transaction implementation
        // 한글: OLEDB 트랜잭션 구현
    }

    void OLEDBConnection::CommitTransaction()
    {
        // English: OLEDB commit implementation
        // 한글: OLEDB 커밋 구현
    }

    void OLEDBConnection::RollbackTransaction()
    {
        // English: OLEDB rollback implementation
        // 한글: OLEDB 롤백 구현
    }

    // =============================================================================
    // English: OLEDBStatement Implementation
    // 한글: OLEDBStatement 구현
    // =============================================================================

    OLEDBStatement::OLEDBStatement() 
        : mPrepared(false)
        , mTimeout(30)
    {
    }

    OLEDBStatement::~OLEDBStatement()
    {
        Close();
    }

    void OLEDBStatement::SetQuery(const std::string& query)
    {
        mQuery = query;
    }

    void OLEDBStatement::SetTimeout(int seconds)
    {
        mTimeout = seconds;
    }

    // English: Simple in-memory parameter binding for module tests
    // 한글: 모듈 테스트용 단순 인메모리 파라미터 바인딩
    void OLEDBStatement::BindParameter(size_t index, const std::string& value)
    {
        if (index == 0) return;
        if (mParameters.size() < index)
        {
            mParameters.resize(index);
        }
        mParameters[index - 1] = value;
    }

    void OLEDBStatement::BindParameter(size_t index, int value)
    {
        BindParameter(index, std::to_string(value));
    }

    void OLEDBStatement::BindParameter(size_t index, long long value)
    {
        BindParameter(index, std::to_string(value));
    }

    void OLEDBStatement::BindParameter(size_t index, double value)
    {
        BindParameter(index, std::to_string(value));
    }

    void OLEDBStatement::BindParameter(size_t index, bool value)
    {
        BindParameter(index, value ? "1" : "0");
    }

    void OLEDBStatement::BindNullParameter(size_t index)
    {
        BindParameter(index, std::string());
    }

    std::unique_ptr<IResultSet> OLEDBStatement::ExecuteQuery()
    {
        // English: For module tests return an empty result set
        // 한글: 모듈 테스트용 빈 결과 집합 반환
        mPrepared = true;
        return std::make_unique<OLEDBResultSet>();
    }

    int OLEDBStatement::ExecuteUpdate()
    {
        // English: No-op update simulation
        // 한글: 업데이트 시뮬레이션
        mPrepared = true;
        return 0;
    }

    bool OLEDBStatement::Execute()
    {
        // English: Execute statement without returning results
        // 한글: 결과 없이 구문 실행
        mPrepared = true;
        return true;
    }

    void OLEDBStatement::AddBatch() 
    {
        // English: Add current query+params to batch
        // 한글: 현재 쿼리+파라미터를 배치에 추가
        if (!mQuery.empty()) 
        {
            // English: Simple serialization: query|p1|p2|...
            // 한글: 단순 직렬화: query|p1|p2|...
            std::string entry = mQuery;
            for (const auto& p : mParameters) 
            {
                entry.push_back('\x1F'); // English: unit separator / 한글: 단위 구분자
                entry += p;
            }
            mBatch.push_back(std::move(entry));
        }
    }

    std::vector<int> OLEDBStatement::ExecuteBatch() 
    {
        std::vector<int> results;
        for (size_t i = 0; i < mBatch.size(); ++i) 
        {
            // English: Simulate execution success
            // 한글: 실행 성공 시뮬레이션
            results.push_back(0);
        }
        mBatch.clear();
        return results;
    }

    void OLEDBStatement::ClearParameters() 
    {
        mParameters.clear();
        mPrepared = false;
    }

    void OLEDBStatement::Close() 
    {
        // English: OLEDB close implementation
        // 한글: OLEDB 닫기 구현
    }

    // =============================================================================
    // English: OLEDBResultSet Implementation
    // 한글: OLEDBResultSet 구현
    // =============================================================================

    OLEDBResultSet::OLEDBResultSet() 
        : mHasData(false)
        , mMetadataLoaded(false) 
    {
    }

    OLEDBResultSet::~OLEDBResultSet() 
    {
        Close();
    }

    void OLEDBResultSet::LoadMetadata() 
    {
        // English: No metadata available in the module test stub
        // 한글: 모듈 테스트 스텁에서는 메타데이터 없음
        mMetadataLoaded = true;
    }

    bool OLEDBResultSet::Next() 
    {
        // English: No rows in stub
        // 한글: 스텁에는 행 없음
        mHasData = false;
        return false;
    }

    bool OLEDBResultSet::IsNull([[maybe_unused]] size_t columnIndex) 
    {
        return true;
    }

    bool OLEDBResultSet::IsNull([[maybe_unused]] const std::string& columnName) 
    {
        return true;
    }

    std::string OLEDBResultSet::GetString([[maybe_unused]] size_t columnIndex) 
    {
        return std::string();
    }

    std::string OLEDBResultSet::GetString([[maybe_unused]] const std::string& columnName) 
    {
        return std::string();
    }

    int OLEDBResultSet::GetInt(size_t columnIndex) 
    {
        try 
        {
            return std::stoi(GetString(columnIndex));
        }
        catch (...) 
        {
            return 0;
        }
    }

    int OLEDBResultSet::GetInt(const std::string& columnName) 
    {
        try 
        {
            return std::stoi(GetString(columnName));
        }
        catch (...) 
        {
            return 0;
        }
    }

    long long OLEDBResultSet::GetLong(size_t columnIndex) 
    {
        try 
        {
            return std::stoll(GetString(columnIndex));
        }
        catch (...) 
        {
            return 0;
        }
    }

    long long OLEDBResultSet::GetLong(const std::string& columnName) 
    {
        try 
        {
            return std::stoll(GetString(columnName));
        }
        catch (...) 
        {
            return 0;
        }
    }

    double OLEDBResultSet::GetDouble(size_t columnIndex) 
    {
        try 
        {
            return std::stod(GetString(columnIndex));
        }
        catch (...) 
        {
            return 0.0;
        }
    }

    double OLEDBResultSet::GetDouble(const std::string& columnName) 
    {
        try 
        {
            return std::stod(GetString(columnName));
        }
        catch (...) 
        {
            return 0.0;
        }
    }

    bool OLEDBResultSet::GetBool(size_t columnIndex) 
    {
        return GetInt(columnIndex) != 0;
    }

    bool OLEDBResultSet::GetBool(const std::string& columnName) 
    {
        return GetInt(columnName) != 0;
    }

    size_t OLEDBResultSet::GetColumnCount() const 
    {
        return mColumnNames.size();
    }

    std::string OLEDBResultSet::GetColumnName(size_t columnIndex) const 
    {
        if (columnIndex >= mColumnNames.size()) 
        {
            throw DatabaseException("Column index out of range");
        }
        return mColumnNames[columnIndex];
    }

    size_t OLEDBResultSet::FindColumn(const std::string& columnName) const 
    {
        for (size_t i = 0; i < mColumnNames.size(); ++i) 
        {
            if (mColumnNames[i] == columnName) 
            {
                return i;
            }
        }
        throw DatabaseException("Column not found: " + columnName);
    }

    void OLEDBResultSet::Close() 
    {
        // English: OLEDB close implementation
        // 한글: OLEDB 닫기 구현
    }

}  // namespace Database
}  // namespace Network
