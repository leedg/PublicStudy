#pragma once

// OLE DB implementation of database interfaces (Windows only)

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

// Forward declarations
class OLEDBConnection;
class OLEDBStatement;
class OLEDBResultSet;

// =============================================================================
// OLEDBDatabase — IDatabase backed by OLE DB / SQLOLEDB provider
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

    // IDatabase::BeginTransaction is intentionally unsupported —
    //          use IConnection (CreateConnection()) for transactional work.
    void BeginTransaction()    override;
    void CommitTransaction()   override;
    void RollbackTransaction() override;

    DatabaseType GetType() const override { return DatabaseType::OLEDB; }
    const DatabaseConfig &GetConfig() const override { return mConfig; }

    // Expose the initialized data source for connection creation
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
// OLEDBConnection — IConnection backed by OLE DB session + ITransactionLocal
// =============================================================================
class OLEDBConnection : public IConnection
{
public:
    // dataSource is borrowed — OLEDBDatabase must outlive this object.
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

    // Exposed for OLEDBStatement::CreateStatement
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
// OLEDBStatement — IStatement backed by ICommandText + IAccessor
// =============================================================================
class OLEDBStatement : public IStatement
{
public:
    // commandFactory is borrowed from OLEDBConnection::mSession.
    //          ownerConn (optional) keeps a per-statement connection alive
    //          when created via IDatabase::CreateStatement().
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
    // Typed parameter slot
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

    // Resize mParams and assign a fixed-size typed slot (int / long long / double).
    //          Guard against index==0 (OLE DB parameters are 1-based; 0 is invalid).
    //          TypeTag is the ParamValue::Type enum; FieldPtr is a member pointer to the value field.
    template<ParamValue::Type TypeTag, auto FieldPtr>
    void SetParam(size_t index, decltype(ParamValue{}.*FieldPtr) value)
    {
        if (index == 0) return;
        if (mParams.size() < index) mParams.resize(index);
        auto &p     = mParams[index - 1];
        p.type      = TypeTag;
        p.*FieldPtr = value;
    }

    // Buffer layout per slot:
    //   [DBSTATUS(4)] [pad(4)] [DBLENGTH(8)] [value(variable)]
    //   DBLENGTH is ULONGLONG (8 bytes) and must be 8-byte aligned.
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
// OLEDBResultSet — IResultSet backed by IRowset + IAccessor
// =============================================================================
class OLEDBResultSet : public IResultSet
{
public:
    // Takes ownership of the rowset (already AddRef'd by Execute).
    explicit OLEDBResultSet(IRowset *rowset);
    virtual ~OLEDBResultSet();

    // IResultSet interface (name overloads inherited from IResultSet default impl)
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
    // Per-row column cache — avoids double-read of forward-only rowset
    struct ColumnData
    {
        bool        fetched = false;
        bool        isNull  = false;
        std::string value;  // UTF-8
    };

    void LoadMetadata();
    const ColumnData &FetchColumn(size_t colIdx);
    void ReleaseCurrentRow();

    // Parse a string column value as a numeric type; returns defaultVal on failure.
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

    // Buffer layout per column:
    //   [DBSTATUS(4)] [pad(4)] [DBLENGTH(8)] [wchar value(kTextBufW)]
    //   DBLENGTH is ULONGLONG (8 bytes), must be 8-byte aligned.
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
