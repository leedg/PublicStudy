#pragma once

// 데이터베이스 인터페이스의 OLE DB 구현 (Windows 전용).
//
// ODBC 대신 OLE DB를 선택하는 경우:
//   - SQL Server 전용 기능(서버 커서, 대량 복사 등)이 필요할 때
//   - ODBC 드라이버 설치 없이 SQLOLEDB/MSOLEDBSQL 공급자를 직접 사용할 때
//   - OLE DB 공급자가 더 나은 성능을 제공하는 레거시 환경
//
// 제약사항:
//   - Windows 전용 (oledb.h, COM, SQLOLEDB 공급자 필요)
//   - COM 초기화(CoInitializeEx)가 필요하다.
//     IOCP 환경에서는 COINIT_MULTITHREADED를 사용해야 스레드 간 COM 객체 공유가 안전하다.

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IConnection.h"
#include "../Interfaces/IDatabase.h"
#include "../Interfaces/IResultSet.h"
#include "../Interfaces/IStatement.h"
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <oledb.h>
#include <oledberr.h>
#include <msdadc.h>
#include <msdaguid.h>
#include <msdasc.h>   // IDataInitialize / CLSID_MSDAINITIALIZE
#include <atomic>

#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "msdasc.lib")

namespace Network
{
namespace Database
{

// 전방 선언
class OLEDBConnection;
class OLEDBStatement;
class OLEDBResultSet;

// =============================================================================
// OLEDBDatabase — OLE DB / SQLOLEDB 공급자를 사용하는 IDatabase 구현
// =============================================================================

class OLEDBDatabase : public IDatabase
{
public:
    OLEDBDatabase();
    virtual ~OLEDBDatabase();

    void Connect(const DatabaseConfig &config) override;
    void Disconnect() override;
    bool IsConnected() const override;

    std::unique_ptr<IConnection> CreateConnection() override;
    std::unique_ptr<IStatement>  CreateStatement()  override;

    // IDatabase::BeginTransaction은 의도적으로 지원하지 않는다.
    // 트랜잭션은 CreateConnection()으로 얻은 IConnection을 사용할 것.
    // (트랜잭션 상태가 연결 단위이기 때문)
    void BeginTransaction()    override;
    void CommitTransaction()   override;
    void RollbackTransaction() override;

    DatabaseType GetType() const override { return DatabaseType::OLEDB; }
    const DatabaseConfig &GetConfig() const override { return mConfig; }

    // 연결 생성 시 초기화된 데이터 소스(IDBInitialize) 제공
    IDBInitialize *GetDataSource() const { return mDataSource; }

private:
    static std::string OLEDBErrorMessage(IUnknown *obj, const IID &iid);

private:
    DatabaseConfig  mConfig;
    IDBInitialize  *mDataSource;
    bool            mCOMInitialized;
    std::atomic<bool> mConnected;
};

// =============================================================================
// OLEDBConnection — OLE DB 세션과 ITransactionLocal을 사용하는 IConnection 구현
// =============================================================================

class OLEDBConnection : public IConnection
{
public:
    // dataSource: OLEDBDatabase 소유 포인터 (빌린 참조).
    //             OLEDBDatabase 인스턴스가 이 객체보다 오래 살아야 한다.
    explicit OLEDBConnection(IDBInitialize *dataSource, const std::string &connStr);
    virtual ~OLEDBConnection();

    void Open(const std::string &connectionString) override;
    void Close() override;
    bool IsOpen() const override;

    std::unique_ptr<IStatement> CreateStatement() override;
    void BeginTransaction()    override;
    void CommitTransaction()   override;
    void RollbackTransaction() override;

    int GetLastErrorCode()  const override { return mLastErrorCode; }
    std::string GetLastError() const override { return mLastError; }

    // OLEDBStatement 생성 시 세션 핸들 제공
    IDBCreateCommand *GetSession() const { return mSession; }

private:
    IDBInitialize    *mDataSource;   // 빌린 포인터 (소유 안 함)
    IDBCreateCommand *mSession;      // AddRef'd
    ITransactionLocal *mTransaction; // QI'd from mSession
    std::string       mConnStr;
    std::string       mLastError;
    int               mLastErrorCode;
    bool              mConnected;
    bool              mInTransaction;
};

// =============================================================================
// OLEDBStatement — ICommandText와 IAccessor를 사용하는 IStatement 구현
// =============================================================================

class OLEDBStatement : public IStatement
{
public:
    // commandFactory: OLEDBConnection::mSession을 빌린다.
    // ownerConn (선택): IDatabase::CreateStatement()에서 생성된 경우
    //   statement 전용 연결의 소유권을 이전받아 수명을 statement와 함께 관리한다.
    explicit OLEDBStatement(IDBCreateCommand *commandFactory,
                            std::unique_ptr<OLEDBConnection> ownerConn = nullptr);
    virtual ~OLEDBStatement();

    void SetQuery(const std::string &query) override;
    void SetTimeout(int seconds) override;

    void BindParameter(size_t index, const std::string &value) override;
    void BindParameter(size_t index, int value)       override;
    void BindParameter(size_t index, long long value) override;
    void BindParameter(size_t index, double value)    override;
    void BindParameter(size_t index, bool value)      override;
    void BindNullParameter(size_t index)              override;

    std::unique_ptr<IResultSet> ExecuteQuery()  override;
    int                         ExecuteUpdate() override;
    bool                        Execute()       override;

    void AddBatch() override;
    std::vector<int> ExecuteBatch() override;

    void ClearParameters() override;
    void Close() override;

private:
    // 타입별 파라미터 슬롯
    struct ParamValue
    {
        enum class Type { Text, Int, Int64, Double, Bool, Null } type = Type::Null;
        std::wstring  text;
        INT32         i32   = 0;
        INT64         i64   = 0;
        double        dbl   = 0.0;
        VARIANT_BOOL  vbool = VARIANT_FALSE;
    };

    struct BatchEntry { std::vector<ParamValue> params; };

    // mParams를 늘리고 고정 크기 타입 슬롯을 채운다 (int / long long / double).
    // index==0 가드 포함 (OLE DB 파라미터는 1-based; 0은 무효).
    // TypeTag는 ParamValue::Type 열거형; FieldPtr은 값 필드의 멤버 포인터.
    template<ParamValue::Type TypeTag, auto FieldPtr>
    void SetParam(size_t index, decltype(ParamValue{}.*FieldPtr) value)
    {
        if (index == 0) return;
        if (mParams.size() < index) mParams.resize(index);
        auto &p     = mParams[index - 1];
        p.type      = TypeTag;
        p.*FieldPtr = value;
    }

    // 슬롯당 버퍼 레이아웃 (파라미터 accessor용):
    //   [DBSTATUS(4)] [패딩(4)] [DBLENGTH(8)] [값(가변)]
    //   DBLENGTH는 ULONGLONG(8바이트)으로 8바이트 정렬 필요.
    static constexpr DBLENGTH kStatusOff  = 0;
    static constexpr DBLENGTH kLengthOff  = 8;          // 8바이트 정렬
    static constexpr DBLENGTH kValueOff   = 16;         // status(4)+패딩(4)+length(8) 이후
    static constexpr DBLENGTH kFixedSlot  = 32;         // 16 헤더 + 8 값 + 8 패딩
    static constexpr DBLENGTH kTextBufW   = 2048 * sizeof(wchar_t); // 최대 2048 wchar
    static constexpr DBLENGTH kTextSlot   = kValueOff + kTextBufW;

    IRowset *ExecuteInternal(DBROWCOUNT *pRowsAffected);
    void     BuildParamAccessor(ICommandText *cmd, HACCESSOR &hAcc,
                                std::vector<BYTE> &buf);
    void     ReleaseCommand();

    std::unique_ptr<OLEDBConnection> mOwnerConn;
    IDBCreateCommand *mCommandFactory; // 빌린 포인터 (소유 안 함)
    std::string       mQuery;
    std::vector<ParamValue> mParams;
    std::vector<BatchEntry> mBatches;
    int               mTimeout;
};

// =============================================================================
// OLEDBResultSet — IRowset과 IAccessor를 사용하는 IResultSet 구현
// =============================================================================

class OLEDBResultSet : public IResultSet
{
public:
    // rowset 소유권을 넘겨받는다 (Execute에서 이미 AddRef됨).
    explicit OLEDBResultSet(IRowset *rowset);
    virtual ~OLEDBResultSet();

    // IResultSet 인터페이스 (이름 오버로드는 IResultSet 기본 구현 상속)
    bool Next() override;
    bool IsNull(size_t columnIndex) override;
    std::string GetString(size_t columnIndex) override;
    int       GetInt(size_t columnIndex) override;
    long long GetLong(size_t columnIndex) override;
    double    GetDouble(size_t columnIndex) override;
    bool      GetBool(size_t columnIndex) override;
    size_t      GetColumnCount() const override;
    std::string GetColumnName(size_t columnIndex) const override;
    size_t      FindColumn(const std::string &columnName) const override;
    void Close() override;

private:
    // 행별 컬럼 캐시 — forward-only 로우셋의 중복 읽기 방지
    struct ColumnData
    {
        bool        fetched = false;
        bool        isNull  = false;
        std::string value;  // UTF-8
    };

    void LoadMetadata();
    const ColumnData &FetchColumn(size_t colIdx);
    void ReleaseCurrentRow();

    // 문자열 컬럼 값을 숫자 타입으로 변환; 실패 시 defaultVal 반환.
    template<typename T>
    static T ParseAs(const std::string &s, T defaultVal) noexcept
    {
        try {
            if constexpr (std::is_same_v<T, int>)       return std::stoi(s);
            if constexpr (std::is_same_v<T, long long>) return std::stoll(s);
            if constexpr (std::is_same_v<T, double>)    return std::stod(s);
        } catch (...) {}
        return defaultVal;
    }

    // 컬럼당 버퍼 레이아웃 (행 accessor용):
    //   [DBSTATUS(4)] [패딩(4)] [DBLENGTH(8)] [wchar 값(kTextBufW)]
    //   DBLENGTH는 ULONGLONG(8바이트)으로 8바이트 정렬 필요.
    static constexpr DBLENGTH kStatusOff = 0;
    static constexpr DBLENGTH kLengthOff = 8;   // 8바이트 정렬
    static constexpr DBLENGTH kValueOff  = 16;  // status(4)+패딩(4)+length(8) 이후
    static constexpr DBLENGTH kTextBufW  = 2048 * sizeof(wchar_t);
    static constexpr DBLENGTH kColSlot   = kValueOff + kTextBufW;

    IRowset    *mRowset;
    IAccessor  *mRowAccessor;
    HACCESSOR   mHRowAccessor;
    HROW        mCurrentRow;
    std::vector<BYTE>        mRowBuffer;
    std::vector<DBBINDING>   mRowBindings;
    std::vector<std::string> mColumnNames;  // 소문자 정규화
    std::vector<ColumnData>  mRowCache;
    bool mMetadataLoaded;
    bool mHasData;
};

} // namespace Database
} // namespace Network

#endif // _WIN32
