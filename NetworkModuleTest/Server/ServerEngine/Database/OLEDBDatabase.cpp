// English: OLEDBDatabase implementation
// ?쒓?: OLEDBDatabase 援ы쁽

#include "OLEDBDatabase.h"
#include <stdexcept>

namespace Network
{
namespace Database
{

// =============================================================================
// English: OLEDBDatabase Implementation
// ?쒓?: OLEDBDatabase 援ы쁽
// =============================================================================

OLEDBDatabase::OLEDBDatabase() : mConnected(false) {}

OLEDBDatabase::~OLEDBDatabase() { Disconnect(); }

void OLEDBDatabase::Connect(const DatabaseConfig &config)
{
	mConfig = config;
	mConnected = true;
}

void OLEDBDatabase::Disconnect() { mConnected = false; }

bool OLEDBDatabase::IsConnected() const { return mConnected; }

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
	// English: OLEDB transaction ??toggle in-transaction flag
	// ?쒓?: OLEDB ?몃옖??뀡 ???몃옖??뀡 ?곹깭 ?뚮옒洹??좉?
	mInTransaction = true;
}

void OLEDBDatabase::CommitTransaction()
{
	// English: OLEDB commit ??clear in-transaction flag
	// ?쒓?: OLEDB 而ㅻ컠 ???몃옖??뀡 ?곹깭 ?뚮옒洹?珥덇린??
	mInTransaction = false;
}

void OLEDBDatabase::RollbackTransaction()
{
	// English: OLEDB rollback ??clear in-transaction flag
	// ?쒓?: OLEDB 濡ㅻ갚 ???몃옖??뀡 ?곹깭 ?뚮옒洹?珥덇린??
	mInTransaction = false;
}

// =============================================================================
// English: OLEDBConnection Implementation
// ?쒓?: OLEDBConnection 援ы쁽
// =============================================================================

OLEDBConnection::OLEDBConnection() : mConnected(false), mLastErrorCode(0) {}

OLEDBConnection::~OLEDBConnection() { Close(); }

void OLEDBConnection::Open([[maybe_unused]] const std::string &connectionString)
{
	if (mConnected)
	{
		return; // English: Already connected / ?쒓?: ?대? ?곌껐??
	}

	// English: OLEDB connection implementation
	// ?쒓?: OLEDB ?곌껐 援ы쁽
	mConnected = true;
}

void OLEDBConnection::Close() { mConnected = false; }

bool OLEDBConnection::IsOpen() const { return mConnected; }

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
	// English: OLEDB connection transaction ??toggle in-transaction flag
	// ?쒓?: OLEDB 而ㅻ꽖???몃옖??뀡 ???몃옖??뀡 ?곹깭 ?뚮옒洹??좉?
	mInTransaction = true;
}

void OLEDBConnection::CommitTransaction()
{
	// English: OLEDB connection commit ??clear in-transaction flag
	// ?쒓?: OLEDB 而ㅻ꽖??而ㅻ컠 ???몃옖??뀡 ?곹깭 ?뚮옒洹?珥덇린??
	mInTransaction = false;
}

void OLEDBConnection::RollbackTransaction()
{
	// English: OLEDB connection rollback ??clear in-transaction flag
	// ?쒓?: OLEDB 而ㅻ꽖??濡ㅻ갚 ???몃옖??뀡 ?곹깭 ?뚮옒洹?珥덇린??
	mInTransaction = false;
}

// =============================================================================
// English: OLEDBStatement Implementation
// ?쒓?: OLEDBStatement 援ы쁽
// =============================================================================

OLEDBStatement::OLEDBStatement() : mPrepared(false), mTimeout(30) {}

OLEDBStatement::~OLEDBStatement() { Close(); }

void OLEDBStatement::SetQuery(const std::string &query) { mQuery = query; }

void OLEDBStatement::SetTimeout(int seconds) { mTimeout = seconds; }

// English: Simple in-memory parameter binding for module tests
// ?쒓?: 紐⑤뱢 ?뚯뒪?몄슜 ?⑥닚 ?몃찓紐⑤━ ?뚮씪誘명꽣 諛붿씤??
void OLEDBStatement::BindParameter(size_t index, const std::string &value)
{
	if (index == 0)
		return;
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
	BindParameter(index, std::string(value ? "1" : "0"));
}

void OLEDBStatement::BindNullParameter(size_t index)
{
	BindParameter(index, std::string());
}

std::unique_ptr<IResultSet> OLEDBStatement::ExecuteQuery()
{
	// English: For module tests return an empty result set
	// ?쒓?: 紐⑤뱢 ?뚯뒪?몄슜 鍮?寃곌낵 吏묓빀 諛섑솚
	mPrepared = true;
	return std::make_unique<OLEDBResultSet>();
}

int OLEDBStatement::ExecuteUpdate()
{
	// English: No-op update simulation
	// ?쒓?: ?낅뜲?댄듃 ?쒕??덉씠??
	mPrepared = true;
	return 0;
}

bool OLEDBStatement::Execute()
{
	// English: Execute statement without returning results
	// ?쒓?: 寃곌낵 ?놁씠 援щЦ ?ㅽ뻾
	mPrepared = true;
	return true;
}

void OLEDBStatement::AddBatch()
{
	// English: Add current query+params to batch
	// ?쒓?: ?꾩옱 荑쇰━+?뚮씪誘명꽣瑜?諛곗튂??異붽?
	if (!mQuery.empty())
	{
		// English: Simple serialization: query|p1|p2|...
		// ?쒓?: ?⑥닚 吏곷젹?? query|p1|p2|...
		std::string entry = mQuery;
		for (const auto &p : mParameters)
		{
			entry.push_back(
				'\x1F'); // English: unit separator / ?쒓?: ?⑥쐞 援щ텇??
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
		// ?쒓?: ?ㅽ뻾 ?깃났 ?쒕??덉씠??
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
	// ?쒓?: OLEDB ?リ린 援ы쁽
}

// =============================================================================
// English: OLEDBResultSet Implementation
// ?쒓?: OLEDBResultSet 援ы쁽
// =============================================================================

OLEDBResultSet::OLEDBResultSet() : mHasData(false), mMetadataLoaded(false) {}

OLEDBResultSet::~OLEDBResultSet() { Close(); }

void OLEDBResultSet::LoadMetadata()
{
	// English: No metadata available in the module test stub
	// ?쒓?: 紐⑤뱢 ?뚯뒪???ㅽ뀅?먯꽌??硫뷀??곗씠???놁쓬
	mMetadataLoaded = true;
}

bool OLEDBResultSet::Next()
{
	// English: No rows in stub
	// ?쒓?: ?ㅽ뀅?먮뒗 ???놁쓬
	mHasData = false;
	return false;
}

bool OLEDBResultSet::IsNull([[maybe_unused]] size_t columnIndex)
{
	return true;
}

bool OLEDBResultSet::IsNull([[maybe_unused]] const std::string &columnName)
{
	return true;
}

std::string OLEDBResultSet::GetString([[maybe_unused]] size_t columnIndex)
{
	return std::string();
}

std::string
OLEDBResultSet::GetString([[maybe_unused]] const std::string &columnName)
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

int OLEDBResultSet::GetInt(const std::string &columnName)
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

long long OLEDBResultSet::GetLong(const std::string &columnName)
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

double OLEDBResultSet::GetDouble(const std::string &columnName)
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

bool OLEDBResultSet::GetBool(const std::string &columnName)
{
	return GetInt(columnName) != 0;
}

size_t OLEDBResultSet::GetColumnCount() const { return mColumnNames.size(); }

std::string OLEDBResultSet::GetColumnName(size_t columnIndex) const
{
	if (columnIndex >= mColumnNames.size())
	{
		throw DatabaseException("Column index out of range");
	}
	return mColumnNames[columnIndex];
}

size_t OLEDBResultSet::FindColumn(const std::string &columnName) const
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
	// ?쒓?: OLEDB ?リ린 援ы쁽
}

} // namespace Database
} // namespace Network
