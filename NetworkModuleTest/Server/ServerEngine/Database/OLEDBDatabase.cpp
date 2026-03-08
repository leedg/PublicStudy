// English: OLE DB implementation of database interfaces (Windows only)
// 한글: 데이터베이스 인터페이스의 OLE DB 구현 (Windows 전용)

#ifdef _WIN32

#include "OLEDBDatabase.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

// English: Wide-string helpers
// 한글: 와이드 문자열 헬퍼
static std::wstring ToWide(const std::string &s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

static std::string ToUTF8(const wchar_t *w, int wlen = -1)
{
    if (!w || wlen == 0) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, wlen, &s[0], n, nullptr, nullptr);
    // strip trailing NUL that WideCharToMultiByte may add when wlen == -1
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// English: Extract OLE DB error description via IErrorInfo
// 한글: IErrorInfo를 통해 OLE DB 오류 설명 추출
static std::string GetOLEDBError(HRESULT hr)
{
    IErrorInfo *pErr = nullptr;
    if (SUCCEEDED(GetErrorInfo(0, &pErr)) && pErr)
    {
        BSTR desc = nullptr;
        pErr->GetDescription(&desc);
        std::string msg;
        if (desc)
        {
            msg = ToUTF8(desc, -1);
            SysFreeString(desc);
        }
        pErr->Release();
        if (!msg.empty()) return msg;
    }
    // fallback: HRESULT hex
    std::ostringstream oss;
    oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
    return oss.str();
}

static void ThrowOLEDB(const char *op, HRESULT hr)
{
    std::string msg = std::string(op) + " failed: " + GetOLEDBError(hr);
    throw Network::Database::DatabaseException(msg, static_cast<int>(hr));
}

namespace Network
{
namespace Database
{

// =============================================================================
// OLEDBDatabase
// =============================================================================

OLEDBDatabase::OLEDBDatabase()
    : mDataSource(nullptr), mCOMInitialized(false), mConnected(false)
{
}

OLEDBDatabase::~OLEDBDatabase()
{
    Disconnect();
}

void OLEDBDatabase::Connect(const DatabaseConfig &config)
{
    if (mConnected) return;

    // English: Initialize COM (MULTITHREADED — safe in IOCP environment)
    // 한글: COM 초기화 (MULTITHREADED — IOCP 환경에서 안전)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        ThrowOLEDB("CoInitializeEx", hr);
    mCOMInitialized = (hr == S_OK); // S_FALSE means already initialized on this thread

    // English: Use IDataInitialize to parse the connection string
    // 한글: IDataInitialize로 연결 문자열을 파싱하여 데이터 소스 생성
    IDataInitialize *pDataInit = nullptr;
    hr = CoCreateInstance(CLSID_MSDAINITIALIZE, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IDataInitialize, reinterpret_cast<void **>(&pDataInit));
    if (FAILED(hr)) ThrowOLEDB("CoCreateInstance(MSDAINITIALIZE)", hr);

    // English: Determine which connection string to use
    // 한글: 사용할 연결 문자열 결정
    std::string connStr = config.mConnectionString;
    if (connStr.empty())
    {
        // English: Build from structured config fields
        // 한글: 구조체 필드에서 연결 문자열 빌드
        connStr = "Provider=SQLOLEDB;"
                  "Data Source=" + config.mHost + "," + std::to_string(config.mPort) + ";"
                  "Initial Catalog=" + config.mDatabase + ";"
                  "User Id=" + config.mUser + ";"
                  "Password=" + config.mPassword + ";";
    }

    std::wstring wConnStr = ToWide(connStr);

    hr = pDataInit->GetDataSource(nullptr, CLSCTX_INPROC_SERVER,
                                  wConnStr.c_str(),
                                  IID_IDBInitialize,
                                  reinterpret_cast<IUnknown **>(&mDataSource));
    pDataInit->Release();
    if (FAILED(hr)) ThrowOLEDB("IDataInitialize::GetDataSource", hr);

    hr = mDataSource->Initialize();
    if (FAILED(hr))
    {
        mDataSource->Release();
        mDataSource = nullptr;
        ThrowOLEDB("IDBInitialize::Initialize", hr);
    }

    mConfig    = config;
    mConnected = true;
}

void OLEDBDatabase::Disconnect()
{
    if (!mConnected) return;
    mConnected = false;
    if (mDataSource)
    {
        mDataSource->Uninitialize();
        mDataSource->Release();
        mDataSource = nullptr;
    }
    if (mCOMInitialized)
    {
        CoUninitialize();
        mCOMInitialized = false;
    }
}

bool OLEDBDatabase::IsConnected() const { return mConnected; }

std::unique_ptr<IConnection> OLEDBDatabase::CreateConnection()
{
    if (!mConnected)
        throw DatabaseException("OLEDBDatabase not connected");
    auto conn = std::make_unique<OLEDBConnection>(mDataSource, mConfig.mConnectionString);
    conn->Open(mConfig.mConnectionString);
    return conn;
}

std::unique_ptr<IStatement> OLEDBDatabase::CreateStatement()
{
    if (!mConnected)
        throw DatabaseException("OLEDBDatabase not connected");

    // English: Create a dedicated connection for this statement (mOwnerConn pattern)
    // 한글: 이 statement 전용 연결 생성 (mOwnerConn 패턴)
    auto conn = std::make_unique<OLEDBConnection>(mDataSource, mConfig.mConnectionString);
    conn->Open(mConfig.mConnectionString);
    IDBCreateCommand *session = conn->GetSession();
    return std::make_unique<OLEDBStatement>(session, std::move(conn));
}

// English: IDatabase-level transactions are not supported for OLE DB —
//          use IConnection (CreateConnection()) for transaction control.
// 한글: OLE DB에서 IDatabase 수준 트랜잭션은 지원하지 않음 —
//       트랜잭션은 CreateConnection()으로 얻은 IConnection 사용.
void OLEDBDatabase::BeginTransaction()
{
    throw DatabaseException(
        "OLEDBDatabase::BeginTransaction is not supported. "
        "Use CreateConnection()->BeginTransaction() instead.");
}
void OLEDBDatabase::CommitTransaction()
{
    throw DatabaseException(
        "OLEDBDatabase::CommitTransaction is not supported. "
        "Use IConnection::CommitTransaction() instead.");
}
void OLEDBDatabase::RollbackTransaction()
{
    throw DatabaseException(
        "OLEDBDatabase::RollbackTransaction is not supported. "
        "Use IConnection::RollbackTransaction() instead.");
}

// =============================================================================
// OLEDBConnection
// =============================================================================

OLEDBConnection::OLEDBConnection(IDBInitialize *dataSource, const std::string &connStr)
    : mDataSource(dataSource), mSession(nullptr), mTransaction(nullptr),
      mConnStr(connStr), mLastErrorCode(0), mConnected(false), mInTransaction(false)
{
}

OLEDBConnection::~OLEDBConnection()
{
    Close();
}

void OLEDBConnection::Open(const std::string & /*connectionString*/)
{
    if (mConnected) return;

    // English: QI IDBCreateSession from the already-initialized data source
    // 한글: 초기화된 데이터 소스에서 IDBCreateSession QI
    IDBCreateSession *pCreateSession = nullptr;
    HRESULT hr = mDataSource->QueryInterface(IID_IDBCreateSession,
                                             reinterpret_cast<void **>(&pCreateSession));
    if (FAILED(hr)) ThrowOLEDB("QI IDBCreateSession", hr);

    // English: Create a session that also exposes IDBCreateCommand
    // 한글: IDBCreateCommand도 노출하는 세션 생성
    hr = pCreateSession->CreateSession(nullptr, IID_IDBCreateCommand,
                                       reinterpret_cast<IUnknown **>(&mSession));
    pCreateSession->Release();
    if (FAILED(hr)) ThrowOLEDB("IDBCreateSession::CreateSession", hr);

    // English: QI ITransactionLocal for explicit transaction control
    // 한글: 명시적 트랜잭션 제어를 위해 ITransactionLocal QI
    hr = mSession->QueryInterface(IID_ITransactionLocal,
                                  reinterpret_cast<void **>(&mTransaction));
    if (FAILED(hr))
    {
        // English: Not all providers support ITransactionLocal — proceed without it
        // 한글: 모든 공급자가 ITransactionLocal을 지원하지는 않음 — 없이 진행
        mTransaction = nullptr;
    }

    mConnected = true;
}

void OLEDBConnection::Close()
{
    if (!mConnected) return;
    mConnected = false;
    if (mInTransaction && mTransaction)
    {
        mTransaction->Abort(nullptr, FALSE, FALSE);
        mInTransaction = false;
    }
    if (mTransaction) { mTransaction->Release(); mTransaction = nullptr; }
    if (mSession)     { mSession->Release();     mSession     = nullptr; }
}

bool OLEDBConnection::IsOpen() const { return mConnected; }

std::unique_ptr<IStatement> OLEDBConnection::CreateStatement()
{
    if (!mConnected)
        throw DatabaseException("OLEDBConnection not open");
    return std::make_unique<OLEDBStatement>(mSession);
}

void OLEDBConnection::BeginTransaction()
{
    if (!mConnected)
        throw DatabaseException("OLEDBConnection not open");
    if (!mTransaction)
        throw DatabaseException("Provider does not support ITransactionLocal");
    if (mInTransaction)
        throw DatabaseException("Transaction already in progress");

    ULONG ulTransId = 0;
    HRESULT hr = mTransaction->StartTransaction(ISOLATIONLEVEL_READCOMMITTED,
                                                0, nullptr, &ulTransId);
    if (FAILED(hr)) ThrowOLEDB("ITransactionLocal::StartTransaction", hr);
    mInTransaction = true;
}

void OLEDBConnection::CommitTransaction()
{
    if (!mInTransaction)
        throw DatabaseException("No active transaction");
    HRESULT hr = mTransaction->Commit(FALSE, XACTTC_SYNC, 0);
    mInTransaction = false;
    if (FAILED(hr)) ThrowOLEDB("ITransaction::Commit", hr);
}

void OLEDBConnection::RollbackTransaction()
{
    if (!mInTransaction)
        throw DatabaseException("No active transaction");
    mTransaction->Abort(nullptr, FALSE, FALSE);
    mInTransaction = false;
}

// =============================================================================
// OLEDBStatement
// =============================================================================

OLEDBStatement::OLEDBStatement(IDBCreateCommand *commandFactory,
                               std::unique_ptr<OLEDBConnection> ownerConn)
    : mOwnerConn(std::move(ownerConn))
    , mCommandFactory(commandFactory)
    , mTimeout(30)
{
}

OLEDBStatement::~OLEDBStatement()
{
    Close();
}

void OLEDBStatement::SetQuery(const std::string &query) { mQuery = query; }
void OLEDBStatement::SetTimeout(int seconds)            { mTimeout = seconds; }

void OLEDBStatement::BindParameter(size_t index, const std::string &value)
{
    if (index == 0) return;
    if (mParams.size() < index) mParams.resize(index);
    auto &p  = mParams[index - 1];
    p.type   = ParamValue::Type::Text;
    p.text   = ToWide(value);
}

void OLEDBStatement::BindParameter(size_t index, int value)
{
    if (index == 0) return;
    if (mParams.size() < index) mParams.resize(index);
    auto &p  = mParams[index - 1];
    p.type   = ParamValue::Type::Int;
    p.i32    = value;
}

void OLEDBStatement::BindParameter(size_t index, long long value)
{
    if (index == 0) return;
    if (mParams.size() < index) mParams.resize(index);
    auto &p  = mParams[index - 1];
    p.type   = ParamValue::Type::Int64;
    p.i64    = value;
}

void OLEDBStatement::BindParameter(size_t index, double value)
{
    if (index == 0) return;
    if (mParams.size() < index) mParams.resize(index);
    auto &p  = mParams[index - 1];
    p.type   = ParamValue::Type::Double;
    p.dbl    = value;
}

void OLEDBStatement::BindParameter(size_t index, bool value)
{
    if (index == 0) return;
    if (mParams.size() < index) mParams.resize(index);
    auto &p  = mParams[index - 1];
    p.type   = ParamValue::Type::Bool;
    p.vbool  = value ? VARIANT_TRUE : VARIANT_FALSE;
}

void OLEDBStatement::BindNullParameter(size_t index)
{
    if (index == 0) return;
    if (mParams.size() < index) mParams.resize(index);
    mParams[index - 1].type = ParamValue::Type::Null;
}

void OLEDBStatement::ClearParameters()
{
    mParams.clear();
}

void OLEDBStatement::Close()
{
    // English: Nothing to release here — command objects are local to Execute*
    // 한글: 여기서 해제할 것 없음 — command 객체는 Execute* 내부에 로컬
}

// English: Build a flat parameter buffer and create an accessor on the command.
//          Buffer layout per slot: DBSTATUS(4) | DBLENGTH(4) | value(variable)
// 한글: 파라미터 버퍼 및 accessor 생성.
//       슬롯당 레이아웃: DBSTATUS(4) | DBLENGTH(4) | 값(가변)
void OLEDBStatement::BuildParamAccessor(ICommandText *cmd, HACCESSOR &hAcc,
                                        std::vector<BYTE> &buf)
{
    size_t n = mParams.size();
    if (n == 0) { hAcc = DB_NULL_HACCESSOR; return; }

    std::vector<DBBINDING> bindings(n);
    DBLENGTH offset = 0;

    for (size_t i = 0; i < n; ++i)
    {
        const auto &p = mParams[i];
        DBBINDING &b  = bindings[i];
        ZeroMemory(&b, sizeof(b));
        b.iOrdinal     = static_cast<DBORDINAL>(i + 1);
        b.dwPart       = DBPART_VALUE | DBPART_STATUS | DBPART_LENGTH;
        b.eParamIO     = DBPARAMIO_INPUT;
        b.obStatus     = offset + kStatusOff;
        b.obLength     = offset + kLengthOff;
        b.obValue      = offset + kValueOff;

        switch (p.type)
        {
        case ParamValue::Type::Int:
            b.wType    = DBTYPE_I4;
            b.cbMaxLen = sizeof(INT32);
            offset    += kFixedSlot;
            break;
        case ParamValue::Type::Int64:
            b.wType    = DBTYPE_I8;
            b.cbMaxLen = sizeof(INT64);
            offset    += kFixedSlot;
            break;
        case ParamValue::Type::Double:
            b.wType    = DBTYPE_R8;
            b.cbMaxLen = sizeof(double);
            offset    += kFixedSlot;
            break;
        case ParamValue::Type::Bool:
            b.wType    = DBTYPE_BOOL;
            b.cbMaxLen = sizeof(VARIANT_BOOL);
            offset    += kFixedSlot;
            break;
        default: // Text or Null — use WSTR
            b.wType    = DBTYPE_WSTR;
            b.cbMaxLen = kTextBufW;
            offset    += kTextSlot;
            break;
        }
    }

    buf.assign(offset, 0);

    // English: Populate each slot in the buffer
    // 한글: 버퍼의 각 슬롯 채우기
    DBLENGTH slotOffset = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const auto &p    = mParams[i];
        BYTE *pStatus    = buf.data() + slotOffset + kStatusOff;
        BYTE *pLength    = buf.data() + slotOffset + kLengthOff;
        BYTE *pValue     = buf.data() + slotOffset + kValueOff;

        if (p.type == ParamValue::Type::Null)
        {
            *reinterpret_cast<DBSTATUS *>(pStatus) = DBSTATUS_S_ISNULL;
            *reinterpret_cast<DBLENGTH *>(pLength) = 0;
            slotOffset += kTextSlot;
            continue;
        }

        *reinterpret_cast<DBSTATUS *>(pStatus) = DBSTATUS_S_OK;

        switch (p.type)
        {
        case ParamValue::Type::Int:
            *reinterpret_cast<DBLENGTH *>(pLength) = sizeof(INT32);
            *reinterpret_cast<INT32 *>(pValue)     = p.i32;
            slotOffset += kFixedSlot;
            break;
        case ParamValue::Type::Int64:
            *reinterpret_cast<DBLENGTH *>(pLength) = sizeof(INT64);
            *reinterpret_cast<INT64 *>(pValue)     = p.i64;
            slotOffset += kFixedSlot;
            break;
        case ParamValue::Type::Double:
            *reinterpret_cast<DBLENGTH *>(pLength) = sizeof(double);
            *reinterpret_cast<double *>(pValue)    = p.dbl;
            slotOffset += kFixedSlot;
            break;
        case ParamValue::Type::Bool:
            *reinterpret_cast<DBLENGTH *>(pLength)      = sizeof(VARIANT_BOOL);
            *reinterpret_cast<VARIANT_BOOL *>(pValue)   = p.vbool;
            slotOffset += kFixedSlot;
            break;
        case ParamValue::Type::Text:
        {
            // English: Copy wide string, max kTextBufW bytes (no NUL counted in DBLENGTH)
            // 한글: 와이드 문자열 복사; DBLENGTH는 NUL 제외 바이트 수
            size_t bytes = p.text.size() * sizeof(wchar_t);
            if (bytes > kTextBufW) bytes = kTextBufW;
            memcpy(pValue, p.text.c_str(), bytes);
            *reinterpret_cast<DBLENGTH *>(pLength) = static_cast<DBLENGTH>(bytes);
            slotOffset += kTextSlot;
            break;
        }
        default:
            slotOffset += kTextSlot;
            break;
        }
    }

    // English: Create parameter accessor
    // 한글: 파라미터 accessor 생성
    IAccessor *pAccessor = nullptr;
    HRESULT hr = cmd->QueryInterface(IID_IAccessor, reinterpret_cast<void **>(&pAccessor));
    if (FAILED(hr)) ThrowOLEDB("QI IAccessor (param)", hr);

    DBBINDSTATUS *pBindStatus = new DBBINDSTATUS[n]();
    hr = pAccessor->CreateAccessor(DBACCESSOR_PARAMETERDATA, n,
                                   bindings.data(), buf.size(),
                                   &hAcc, pBindStatus);
    delete[] pBindStatus;
    pAccessor->Release();
    if (FAILED(hr)) ThrowOLEDB("CreateAccessor(PARAMETERDATA)", hr);
}

IRowset *OLEDBStatement::ExecuteInternal(DBROWCOUNT *pRowsAffected)
{
    if (!mCommandFactory)
        throw DatabaseException("OLEDBStatement: no command factory");
    if (mQuery.empty())
        throw DatabaseException("OLEDBStatement: query not set");

    // English: Create command object
    // 한글: 커맨드 객체 생성
    ICommandText *pCmd = nullptr;
    HRESULT hr = mCommandFactory->CreateCommand(nullptr, IID_ICommandText,
                                                reinterpret_cast<IUnknown **>(&pCmd));
    if (FAILED(hr)) ThrowOLEDB("IDBCreateCommand::CreateCommand", hr);

    // English: Set the query text (DBGUID_DEFAULT lets the provider parse the dialect)
    // 한글: 쿼리 텍스트 설정 (DBGUID_DEFAULT로 공급자가 방언 파싱)
    std::wstring wQuery = ToWide(mQuery);
    hr = pCmd->SetCommandText(DBGUID_DEFAULT, wQuery.c_str());
    if (FAILED(hr)) { pCmd->Release(); ThrowOLEDB("ICommandText::SetCommandText", hr); }

    // English: Build parameter accessor (if parameters are bound)
    // 한글: 파라미터 accessor 빌드 (파라미터가 있을 때)
    HACCESSOR hParamAcc = DB_NULL_HACCESSOR;
    std::vector<BYTE> paramBuf;
    if (!mParams.empty())
    {
        try { BuildParamAccessor(pCmd, hParamAcc, paramBuf); }
        catch (...) { pCmd->Release(); throw; }
    }

    // English: Build DBPARAMS (pass nullptr if no parameters)
    // 한글: DBPARAMS 빌드 (파라미터 없으면 nullptr)
    DBPARAMS dbParams{};
    DBPARAMS *pDbParams = nullptr;
    if (hParamAcc != DB_NULL_HACCESSOR)
    {
        dbParams.pData    = paramBuf.data();
        dbParams.cParamSets = 1;
        dbParams.hAccessor  = hParamAcc;
        pDbParams           = &dbParams;
    }

    // English: Execute the command
    // 한글: 커맨드 실행
    DBROWCOUNT rowsAffected = 0;
    IRowset *pRowset        = nullptr;
    hr = pCmd->Execute(nullptr, IID_IRowset,
                       pDbParams, &rowsAffected,
                       reinterpret_cast<IUnknown **>(&pRowset));

    // English: Release parameter accessor before checking result
    // 한글: 결과 확인 전 파라미터 accessor 해제
    if (hParamAcc != DB_NULL_HACCESSOR)
    {
        IAccessor *pAcc = nullptr;
        if (SUCCEEDED(pCmd->QueryInterface(IID_IAccessor,
                      reinterpret_cast<void **>(&pAcc))))
        {
            pAcc->ReleaseAccessor(hParamAcc, nullptr);
            pAcc->Release();
        }
    }
    pCmd->Release();

    if (FAILED(hr) && hr != DB_S_NORESULT)
        ThrowOLEDB("ICommandText::Execute", hr);

    if (pRowsAffected) *pRowsAffected = rowsAffected;
    return pRowset; // may be nullptr for non-SELECT statements
}

std::unique_ptr<IResultSet> OLEDBStatement::ExecuteQuery()
{
    IRowset *pRowset = ExecuteInternal(nullptr);
    if (!pRowset)
        throw DatabaseException("ExecuteQuery: command returned no rowset");
    return std::make_unique<OLEDBResultSet>(pRowset);
}

int OLEDBStatement::ExecuteUpdate()
{
    DBROWCOUNT rows = 0;
    IRowset *pRowset = ExecuteInternal(&rows);
    if (pRowset) pRowset->Release();
    return static_cast<int>(rows);
}

bool OLEDBStatement::Execute()
{
    DBROWCOUNT rows = 0;
    IRowset *pRowset = ExecuteInternal(&rows);
    if (pRowset) pRowset->Release();
    return true;
}

void OLEDBStatement::AddBatch()
{
    mBatches.push_back(BatchEntry{mParams});
    mParams.clear();
}

std::vector<int> OLEDBStatement::ExecuteBatch()
{
    std::vector<int> results;
    for (auto &entry : mBatches)
    {
        mParams = std::move(entry.params);
        DBROWCOUNT rows = 0;
        IRowset *pRowset = nullptr;
        try { pRowset = ExecuteInternal(&rows); }
        catch (...) { results.push_back(-1); mParams.clear(); continue; }
        if (pRowset) pRowset->Release();
        results.push_back(static_cast<int>(rows));
        mParams.clear();
    }
    mBatches.clear();
    return results;
}

// =============================================================================
// OLEDBResultSet
// =============================================================================

OLEDBResultSet::OLEDBResultSet(IRowset *rowset)
    : mRowset(rowset)
    , mRowAccessor(nullptr)
    , mHRowAccessor(DB_NULL_HACCESSOR)
    , mCurrentRow(DB_NULL_HROW)
    , mMetadataLoaded(false)
    , mHasData(false)
{
}

OLEDBResultSet::~OLEDBResultSet()
{
    Close();
}

void OLEDBResultSet::Close()
{
    ReleaseCurrentRow();
    if (mRowAccessor && mHRowAccessor != DB_NULL_HACCESSOR)
    {
        mRowAccessor->ReleaseAccessor(mHRowAccessor, nullptr);
        mHRowAccessor = DB_NULL_HACCESSOR;
    }
    if (mRowAccessor) { mRowAccessor->Release(); mRowAccessor = nullptr; }
    if (mRowset)      { mRowset->Release();      mRowset      = nullptr; }
}

void OLEDBResultSet::ReleaseCurrentRow()
{
    if (mCurrentRow != DB_NULL_HROW && mRowset)
    {
        mRowset->ReleaseRows(1, &mCurrentRow, nullptr, nullptr, nullptr);
        mCurrentRow = DB_NULL_HROW;
    }
}

void OLEDBResultSet::LoadMetadata()
{
    if (mMetadataLoaded) return;
    mMetadataLoaded = true;

    // English: QI IColumnsInfo to get column metadata
    // 한글: IColumnsInfo QI로 컬럼 메타데이터 획득
    IColumnsInfo *pColInfo = nullptr;
    HRESULT hr = mRowset->QueryInterface(IID_IColumnsInfo,
                                         reinterpret_cast<void **>(&pColInfo));
    if (FAILED(hr)) ThrowOLEDB("QI IColumnsInfo", hr);

    DBORDINAL nCols = 0;
    DBCOLUMNINFO *pCI = nullptr;
    OLECHAR *pStrBuf  = nullptr;
    hr = pColInfo->GetColumnInfo(&nCols, &pCI, &pStrBuf);
    pColInfo->Release();
    if (FAILED(hr)) ThrowOLEDB("IColumnsInfo::GetColumnInfo", hr);

    // English: Skip bookmark column (iOrdinal == 0) — bind all data columns as WSTR
    // 한글: 북마크 컬럼(iOrdinal==0) 스킵 — 모든 데이터 컬럼을 WSTR로 바인딩
    DBLENGTH rowBufOffset = 0;
    for (DBORDINAL i = 0; i < nCols; ++i)
    {
        if (pCI[i].iOrdinal == 0) continue; // skip bookmark

        // English: Column name — lower-case for case-insensitive lookup
        // 한글: 컬럼명 — 대소문자 무관 조회를 위해 소문자 변환
        std::string name;
        if (pCI[i].pwszName)
            name = ToUTF8(pCI[i].pwszName, -1);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c){ return static_cast<char>(::tolower(c)); });
        mColumnNames.push_back(name);

        DBBINDING b{};
        b.iOrdinal = pCI[i].iOrdinal;
        b.dwPart   = DBPART_VALUE | DBPART_STATUS | DBPART_LENGTH;
        b.wType    = DBTYPE_WSTR;
        b.cbMaxLen = kTextBufW;
        b.obStatus = rowBufOffset + kStatusOff;
        b.obLength = rowBufOffset + kLengthOff;
        b.obValue  = rowBufOffset + kValueOff;
        mRowBindings.push_back(b);
        rowBufOffset += kColSlot;
    }

    CoTaskMemFree(pCI);
    CoTaskMemFree(pStrBuf);

    mRowBuffer.assign(rowBufOffset, 0);
    mRowCache.assign(mColumnNames.size(), ColumnData{});

    // English: Create row accessor
    // 한글: 행 accessor 생성
    hr = mRowset->QueryInterface(IID_IAccessor,
                                 reinterpret_cast<void **>(&mRowAccessor));
    if (FAILED(hr)) ThrowOLEDB("QI IAccessor (row)", hr);

    size_t nBindings = mRowBindings.size();
    std::vector<DBBINDSTATUS> bindStatus(nBindings, DBBINDSTATUS_OK);
    hr = mRowAccessor->CreateAccessor(DBACCESSOR_ROWDATA, nBindings,
                                      mRowBindings.data(), mRowBuffer.size(),
                                      &mHRowAccessor, bindStatus.data());
    if (FAILED(hr)) ThrowOLEDB("CreateAccessor(ROWDATA)", hr);
}

bool OLEDBResultSet::Next()
{
    if (!mRowset) return false;

    LoadMetadata();
    ReleaseCurrentRow();

    // English: Invalidate row cache
    // 한글: 행 캐시 무효화
    for (auto &c : mRowCache) c = ColumnData{};

    DBCOUNTITEM nFetched = 0;
    HROW hRow            = DB_NULL_HROW;
    HROW *pRow           = &hRow;
    HRESULT hr = mRowset->GetNextRows(DB_NULL_HCHAPTER, 0, 1, &nFetched, &pRow);

    if (hr == DB_S_ENDOFROWSET || nFetched == 0)
    {
        mHasData = false;
        return false;
    }
    if (FAILED(hr)) ThrowOLEDB("IRowset::GetNextRows", hr);

    mCurrentRow = hRow;

    // English: Fetch row data into buffer
    // 한글: 행 데이터를 버퍼로 가져오기
    hr = mRowset->GetData(mCurrentRow, mHRowAccessor, mRowBuffer.data());
    if (FAILED(hr)) ThrowOLEDB("IRowset::GetData", hr);

    mHasData = true;
    return true;
}

const OLEDBResultSet::ColumnData &OLEDBResultSet::FetchColumn(size_t colIdx)
{
    if (colIdx >= mRowCache.size())
        throw DatabaseException("Column index out of range");

    auto &slot = mRowCache[colIdx];
    if (slot.fetched) return slot;
    slot.fetched = true;

    const BYTE *pStatus = mRowBuffer.data() + mRowBindings[colIdx].obStatus;
    const BYTE *pLength = mRowBuffer.data() + mRowBindings[colIdx].obLength;
    const BYTE *pValue  = mRowBuffer.data() + mRowBindings[colIdx].obValue;

    DBSTATUS status = *reinterpret_cast<const DBSTATUS *>(pStatus);
    if (status == DBSTATUS_S_ISNULL || status == DBSTATUS_S_DEFAULT)
    {
        slot.isNull = true;
        return slot;
    }

    DBLENGTH byteLen = *reinterpret_cast<const DBLENGTH *>(pLength);
    // English: byteLen is byte count of wide chars; convert to wchar count
    // 한글: byteLen은 와이드 문자의 바이트 수; wchar 개수로 변환
    int wcharCount = static_cast<int>(byteLen / sizeof(wchar_t));
    slot.value = ToUTF8(reinterpret_cast<const wchar_t *>(pValue), wcharCount);
    return slot;
}

bool OLEDBResultSet::IsNull(size_t columnIndex)
{
    return FetchColumn(columnIndex).isNull;
}
bool OLEDBResultSet::IsNull(const std::string &columnName)
{
    return IsNull(FindColumn(columnName));
}

std::string OLEDBResultSet::GetString(size_t columnIndex)
{
    const auto &c = FetchColumn(columnIndex);
    return c.isNull ? std::string{} : c.value;
}
std::string OLEDBResultSet::GetString(const std::string &columnName)
{
    return GetString(FindColumn(columnName));
}

int OLEDBResultSet::GetInt(size_t columnIndex)
{
    const auto &c = FetchColumn(columnIndex);
    if (c.isNull) return 0;
    try { return std::stoi(c.value); } catch (...) { return 0; }
}
int OLEDBResultSet::GetInt(const std::string &columnName)
{
    return GetInt(FindColumn(columnName));
}

long long OLEDBResultSet::GetLong(size_t columnIndex)
{
    const auto &c = FetchColumn(columnIndex);
    if (c.isNull) return 0LL;
    try { return std::stoll(c.value); } catch (...) { return 0LL; }
}
long long OLEDBResultSet::GetLong(const std::string &columnName)
{
    return GetLong(FindColumn(columnName));
}

double OLEDBResultSet::GetDouble(size_t columnIndex)
{
    const auto &c = FetchColumn(columnIndex);
    if (c.isNull) return 0.0;
    try { return std::stod(c.value); } catch (...) { return 0.0; }
}
double OLEDBResultSet::GetDouble(const std::string &columnName)
{
    return GetDouble(FindColumn(columnName));
}

bool OLEDBResultSet::GetBool(size_t columnIndex)
{
    const auto &c = FetchColumn(columnIndex);
    if (c.isNull) return false;
    // English: Accept "1", "-1" (VARIANT_TRUE), "true", "TRUE", non-zero strings
    // 한글: "1", "-1"(VARIANT_TRUE), "true", "TRUE", 0이 아닌 숫자 문자열 허용
    if (c.value == "1" || c.value == "-1") return true;
    if (c.value == "0") return false;
    std::string lower = c.value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch){ return static_cast<char>(::tolower(ch)); });
    if (lower == "true" || lower == "yes") return true;
    try { return std::stod(c.value) != 0.0; } catch (...) { return false; }
}
bool OLEDBResultSet::GetBool(const std::string &columnName)
{
    return GetBool(FindColumn(columnName));
}

size_t OLEDBResultSet::GetColumnCount() const
{
    return mColumnNames.size();
}

std::string OLEDBResultSet::GetColumnName(size_t columnIndex) const
{
    if (columnIndex >= mColumnNames.size())
        throw DatabaseException("Column index out of range");
    return mColumnNames[columnIndex];
}

size_t OLEDBResultSet::FindColumn(const std::string &columnName) const
{
    // English: Case-insensitive search (both stored and query name lower-cased)
    // 한글: 대소문자 무관 검색 (저장된 이름과 조회 이름 모두 소문자)
    std::string lower = columnName;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return static_cast<char>(::tolower(c)); });
    for (size_t i = 0; i < mColumnNames.size(); ++i)
        if (mColumnNames[i] == lower) return i;
    throw DatabaseException("Column not found: " + columnName);
}

} // namespace Database
} // namespace Network

#endif // _WIN32
