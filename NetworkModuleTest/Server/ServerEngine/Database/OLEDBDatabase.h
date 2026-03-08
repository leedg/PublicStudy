#pragma once

// English: OLE DB implementation of database interfaces (Windows only)
// 한글: 데이터베이스 인터페이스의 OLE DB 구현 (Windows 전용)

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IConnection.h"
#include "../Interfaces/IDatabase.h"
#include "../Interfaces/IResultSet.h"
#include "../Interfaces/IStatement.h"
#include <memory>
#include <string>
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

// English: Forward declarations
// 한글: 전방 선언
class OLEDBConnection;
class OLEDBStatement;
class OLEDBResultSet;

// =============================================================================
// English: OLEDBDatabase — IDatabase backed by OLE DB / SQLOLEDB provider
// 한글: OLE DB / SQLOLEDB 공급자를 사용하는 IDatabase 구현
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

    // English: IDatabase::BeginTransaction is intentionally unsupported —
    //          use IConnection (CreateConnection()) for transactional work.
    // 한글: IDatabase::BeginTransaction은 의도적으로 지원하지 않음 —
    //       트랜잭션은 CreateConnection()으로 얻은 IConnection을 사용할 것.
    void BeginTransaction()    override;
    void CommitTransaction()   override;
    void RollbackTransaction() override;

    DatabaseType GetType() const override { return DatabaseType::OLEDB; }
    const DatabaseConfig &GetConfig() const override { return mConfig; }

    // English: Expose the initialized data source for connection creation
    // 한글: 연결 생성을 위해 초기화된 데이터 소스 노출
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
// English: OLEDBConnection — IConnection backed by OLE DB session + ITransactionLocal
// 한글: OLE DB 세션과 ITransactionLocal을 사용하는 IConnection 구현
// =============================================================================
class OLEDBConnection : public IConnection
{
public:
    // English: dataSource is borrowed — OLEDBDatabase must outlive this object.
    // 한글: dataSource는 빌려온 포인터 — OLEDBDatabase가 더 오래 살아야 함.
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

    // English: Exposed for OLEDBStatement::CreateStatement
    // 한글: OLEDBStatement 생성 시 사용
    IDBCreateCommand *GetSession() const { return mSession; }

private:
    IDBInitialize    *mDataSource;   // borrowed
    IDBCreateCommand *mSession;      // AddRef'd
    ITransactionLocal *mTransaction; // QI'd from mSession
    std::string       mConnStr;
    std::string       mLastError;
    int               mLastErrorCode;
    bool              mConnected;
    bool              mInTransaction;
};

// =============================================================================
// English: OLEDBStatement — IStatement backed by ICommandText + IAccessor
// 한글: ICommandText와 IAccessor를 사용하는 IStatement 구현
// =============================================================================
class OLEDBStatement : public IStatement
{
public:
    // English: commandFactory is borrowed from OLEDBConnection::mSession.
    //          ownerConn (optional) keeps a per-statement connection alive
    //          when created via IDatabase::CreateStatement().
    // 한글: commandFactory는 OLEDBConnection::mSession을 빌림.
    //       ownerConn(선택)은 IDatabase::CreateStatement()의 연결 수명 유지용.
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
    // English: Typed parameter slot
    // 한글: 타입별 파라미터 슬롯
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

    // English: Buffer layout per slot:
    //   [DBSTATUS(4)] [pad(4)] [DBLENGTH(8)] [value(variable)]
    //   DBLENGTH is ULONGLONG (8 bytes) and must be 8-byte aligned.
    // 한글: 슬롯당 버퍼 레이아웃:
    //   [DBSTATUS(4)] [패딩(4)] [DBLENGTH(8)] [값(가변)]
    //   DBLENGTH는 ULONGLONG(8바이트)으로 8바이트 정렬 필요.
    static constexpr DBLENGTH kStatusOff  = 0;
    static constexpr DBLENGTH kLengthOff  = 8;          // 8-byte aligned
    static constexpr DBLENGTH kValueOff   = 16;         // after status(4)+pad(4)+length(8)
    static constexpr DBLENGTH kFixedSlot  = 32;         // 16 header + 8 value + 8 pad
    static constexpr DBLENGTH kTextBufW   = 2048 * sizeof(wchar_t); // 2048 wchar
    static constexpr DBLENGTH kTextSlot   = kValueOff + kTextBufW;

    IRowset *ExecuteInternal(DBROWCOUNT *pRowsAffected);
    void     BuildParamAccessor(ICommandText *cmd, HACCESSOR &hAcc,
                                std::vector<BYTE> &buf);
    void     ReleaseCommand();

    std::unique_ptr<OLEDBConnection> mOwnerConn;
    IDBCreateCommand *mCommandFactory; // borrowed
    std::string       mQuery;
    std::vector<ParamValue> mParams;
    std::vector<BatchEntry> mBatches;
    int               mTimeout;
};

// =============================================================================
// English: OLEDBResultSet — IResultSet backed by IRowset + IAccessor
// 한글: IRowset과 IAccessor를 사용하는 IResultSet 구현
// =============================================================================
class OLEDBResultSet : public IResultSet
{
public:
    // English: Takes ownership of the rowset (already AddRef'd by Execute).
    // 한글: rowset 소유권을 넘겨받음 (Execute에서 이미 AddRef됨).
    explicit OLEDBResultSet(IRowset *rowset);
    virtual ~OLEDBResultSet();

    bool Next() override;
    bool IsNull(size_t columnIndex) override;
    bool IsNull(const std::string &columnName) override;

    std::string GetString(size_t columnIndex) override;
    std::string GetString(const std::string &columnName) override;

    int       GetInt(size_t columnIndex) override;
    int       GetInt(const std::string &columnName) override;
    long long GetLong(size_t columnIndex) override;
    long long GetLong(const std::string &columnName) override;
    double    GetDouble(size_t columnIndex) override;
    double    GetDouble(const std::string &columnName) override;
    bool      GetBool(size_t columnIndex) override;
    bool      GetBool(const std::string &columnName) override;

    size_t      GetColumnCount() const override;
    std::string GetColumnName(size_t columnIndex) const override;
    size_t      FindColumn(const std::string &columnName) const override;

    void Close() override;

private:
    // English: Per-row column cache — avoids double-read of forward-only rowset
    // 한글: 행별 컬럼 캐시 — forward-only 로우셋의 중복 읽기 방지
    struct ColumnData
    {
        bool        fetched = false;
        bool        isNull  = false;
        std::string value;  // UTF-8
    };

    void LoadMetadata();
    const ColumnData &FetchColumn(size_t colIdx);
    void ReleaseCurrentRow();

    // English: Buffer layout per column:
    //   [DBSTATUS(4)] [pad(4)] [DBLENGTH(8)] [wchar value(kTextBufW)]
    //   DBLENGTH is ULONGLONG (8 bytes), must be 8-byte aligned.
    // 한글: 컬럼당 버퍼 레이아웃:
    //   [DBSTATUS(4)] [패딩(4)] [DBLENGTH(8)] [wchar 값(kTextBufW)]
    static constexpr DBLENGTH kStatusOff = 0;
    static constexpr DBLENGTH kLengthOff = 8;   // 8-byte aligned
    static constexpr DBLENGTH kValueOff  = 16;  // after status(4)+pad(4)+length(8)
    static constexpr DBLENGTH kTextBufW  = 2048 * sizeof(wchar_t);
    static constexpr DBLENGTH kColSlot   = kValueOff + kTextBufW;

    IRowset    *mRowset;
    IAccessor  *mRowAccessor;
    HACCESSOR   mHRowAccessor;
    HROW        mCurrentRow;
    std::vector<BYTE>        mRowBuffer;
    std::vector<DBBINDING>   mRowBindings;
    std::vector<std::string> mColumnNames;  // lower-case
    std::vector<ColumnData>  mRowCache;
    bool mMetadataLoaded;
    bool mHasData;
};

} // namespace Database
} // namespace Network

#endif // _WIN32
