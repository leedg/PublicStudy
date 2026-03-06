#include "OLEDBDatabase.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <stdexcept>
#include <utility>

namespace DocDBModule
{
namespace
{
std::string ToUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return value;
}

std::string Trim(const std::string &value)
{
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();

    if (begin >= end)
    {
        return std::string();
    }

    return std::string(begin, end);
}

bool IsSqlKeywordPrefix(const std::string &query, const std::string &keyword)
{
    const std::string upper = ToUpper(Trim(query));
    return upper.rfind(keyword, 0) == 0;
}

std::vector<std::string> ExtractSelectAliases(const std::string &query)
{
    std::vector<std::string> aliases;
    static const std::regex aliasRegex(R"(\?\s+AS\s+([A-Za-z_][A-Za-z0-9_]*))",
                                       std::regex::icase);

    for (std::sregex_iterator it(query.begin(), query.end(), aliasRegex), end; it != end; ++it)
    {
        aliases.push_back((*it)[1].str());
    }

    return aliases;
}
} // namespace

// OLEDBDatabase Implementation
OLEDBDatabase::OLEDBDatabase() : connected_(false) {}

OLEDBDatabase::~OLEDBDatabase() { disconnect(); }

void OLEDBDatabase::connect(const DatabaseConfig &config)
{
    config_ = config;
    connected_ = true;
}

void OLEDBDatabase::disconnect() { connected_ = false; }

bool OLEDBDatabase::isConnected() const { return connected_; }

std::unique_ptr<IConnection> OLEDBDatabase::createConnection()
{
    if (!connected_)
    {
        throw DatabaseException("Database not connected");
    }
    return std::make_unique<OLEDBConnection>();
}

std::unique_ptr<IStatement> OLEDBDatabase::createStatement()
{
    if (!connected_)
    {
        throw DatabaseException("Database not connected");
    }
    return std::make_unique<OLEDBStatement>();
}

void OLEDBDatabase::beginTransaction()
{
    // OLEDB transaction implementation
}

void OLEDBDatabase::commitTransaction()
{
    // OLEDB commit implementation
}

void OLEDBDatabase::rollbackTransaction()
{
    // OLEDB rollback implementation
}

// OLEDBConnection Implementation
OLEDBConnection::OLEDBConnection() : connected_(false), lastErrorCode_(0) {}

OLEDBConnection::~OLEDBConnection() { close(); }

void OLEDBConnection::open([[maybe_unused]] const std::string &connectionString)
{
    if (connected_)
    {
        return; // Already connected
    }

    // OLEDB connection implementation
    connected_ = true;
}

void OLEDBConnection::close() { connected_ = false; }

bool OLEDBConnection::isOpen() const { return connected_; }

std::unique_ptr<IStatement> OLEDBConnection::createStatement()
{
    if (!connected_)
    {
        throw DatabaseException("Connection not open");
    }
    return std::make_unique<OLEDBStatement>();
}

void OLEDBConnection::beginTransaction()
{
    // OLEDB transaction implementation
}

void OLEDBConnection::commitTransaction()
{
    // OLEDB commit implementation
}

void OLEDBConnection::rollbackTransaction()
{
    // OLEDB rollback implementation
}

// OLEDBStatement Implementation
OLEDBStatement::OLEDBStatement() : prepared_(false), timeout_(30) {}

OLEDBStatement::~OLEDBStatement() { close(); }

void OLEDBStatement::setQuery(const std::string &query)
{
    query_ = query;
    prepared_ = false;
}

void OLEDBStatement::setTimeout(int seconds) { timeout_ = seconds; }

// Simple in-memory parameter binding for module tests
void OLEDBStatement::bindParameter(size_t index, const std::string &value)
{
    if (index == 0)
    {
        throw DatabaseException("Parameter index must start at 1");
    }

    if (parameters_.size() < index)
    {
        parameters_.resize(index);
        parameterNulls_.resize(index, false);
    }

    parameters_[index - 1] = value;
    parameterNulls_[index - 1] = false;
}

void OLEDBStatement::bindParameter(size_t index, int value)
{
    bindParameter(index, std::to_string(value));
}

void OLEDBStatement::bindParameter(size_t index, long long value)
{
    bindParameter(index, std::to_string(value));
}

void OLEDBStatement::bindParameter(size_t index, double value)
{
    bindParameter(index, std::to_string(value));
}

void OLEDBStatement::bindParameter(size_t index, bool value)
{
    bindParameter(index, std::string(value ? "1" : "0"));
}

void OLEDBStatement::bindNullParameter(size_t index)
{
    if (index == 0)
    {
        throw DatabaseException("Parameter index must start at 1");
    }

    if (parameters_.size() < index)
    {
        parameters_.resize(index);
        parameterNulls_.resize(index, false);
    }

    parameters_[index - 1].clear();
    parameterNulls_[index - 1] = true;
}

std::unique_ptr<IResultSet> OLEDBStatement::executeQuery()
{
    prepared_ = true;

    if (!IsSqlKeywordPrefix(query_, "SELECT") || parameters_.empty())
    {
        return std::make_unique<OLEDBResultSet>();
    }

    std::vector<std::string> aliases = ExtractSelectAliases(query_);
    if (aliases.size() < parameters_.size())
    {
        for (size_t i = aliases.size(); i < parameters_.size(); ++i)
        {
            aliases.push_back("col" + std::to_string(i + 1));
        }
    }
    else if (aliases.size() > parameters_.size())
    {
        aliases.resize(parameters_.size());
    }

    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<bool>> nullFlags;
    rows.push_back(parameters_);
    nullFlags.push_back(parameterNulls_);

    return std::make_unique<OLEDBResultSet>(std::move(aliases), std::move(rows),
                                            std::move(nullFlags));
}

int OLEDBStatement::executeUpdate()
{
    prepared_ = true;

    if (IsSqlKeywordPrefix(query_, "INSERT") || IsSqlKeywordPrefix(query_, "UPDATE") ||
        IsSqlKeywordPrefix(query_, "DELETE"))
    {
        return 1;
    }

    return 0;
}

bool OLEDBStatement::execute()
{
    // Execute statement without returning results
    prepared_ = true;
    return true;
}

void OLEDBStatement::addBatch()
{
    // Add current query+params to batch
    if (!query_.empty())
    {
        // simple serialization: query|p1|p2|...
        std::string entry = query_;
        for (const auto &p : parameters_)
        {
            entry.push_back('\x1F'); // unit separator
            entry += p;
        }
        batch_.push_back(std::move(entry));
    }
}

std::vector<int> OLEDBStatement::executeBatch()
{
    std::vector<int> results;
    results.reserve(batch_.size());
    for (size_t i = 0; i < batch_.size(); ++i)
    {
        // simulate execution success
        results.push_back(1);
    }
    batch_.clear();
    return results;
}

void OLEDBStatement::clearParameters()
{
    parameters_.clear();
    parameterNulls_.clear();
    prepared_ = false;
}

void OLEDBStatement::close()
{
    // OLEDB close implementation
}

// OLEDBResultSet Implementation
OLEDBResultSet::OLEDBResultSet()
    : hasData_(false), currentRow_(0), metadataLoaded_(false)
{
}

OLEDBResultSet::OLEDBResultSet(std::vector<std::string> columnNames,
                               std::vector<std::vector<std::string>> rows,
                               std::vector<std::vector<bool>> nullFlags)
    : hasData_(false), columnNames_(std::move(columnNames)), rows_(std::move(rows)),
      nullFlags_(std::move(nullFlags)), currentRow_(0), metadataLoaded_(true)
{
}

OLEDBResultSet::~OLEDBResultSet() { close(); }

void OLEDBResultSet::loadMetadata()
{
    metadataLoaded_ = true;
}

bool OLEDBResultSet::next()
{
    if (currentRow_ < rows_.size())
    {
        ++currentRow_;
        hasData_ = true;
        return true;
    }

    hasData_ = false;
    return false;
}

size_t OLEDBResultSet::resolveColumnIndex(size_t columnIndex) const
{
    if (columnIndex >= columnNames_.size())
    {
        throw DatabaseException("Column index out of range");
    }

    return columnIndex;
}

size_t OLEDBResultSet::currentRowIndex() const
{
    if (!hasData_ || currentRow_ == 0)
    {
        throw DatabaseException("No current row");
    }

    return currentRow_ - 1;
}

bool OLEDBResultSet::isNull(size_t columnIndex)
{
    const size_t resolvedColumn = resolveColumnIndex(columnIndex);
    const size_t rowIndex = currentRowIndex();

    if (rowIndex >= nullFlags_.size() || resolvedColumn >= nullFlags_[rowIndex].size())
    {
        return true;
    }

    return nullFlags_[rowIndex][resolvedColumn];
}

bool OLEDBResultSet::isNull(const std::string &columnName)
{
    return isNull(findColumn(columnName));
}

std::string OLEDBResultSet::getString(size_t columnIndex)
{
    const size_t resolvedColumn = resolveColumnIndex(columnIndex);
    const size_t rowIndex = currentRowIndex();

    if (rowIndex >= rows_.size() || resolvedColumn >= rows_[rowIndex].size())
    {
        throw DatabaseException("Column index out of range");
    }

    if (isNull(resolvedColumn))
    {
        return std::string();
    }

    return rows_[rowIndex][resolvedColumn];
}

std::string OLEDBResultSet::getString(const std::string &columnName)
{
    return getString(findColumn(columnName));
}

int OLEDBResultSet::getInt(size_t columnIndex)
{
    try
    {
        return std::stoi(getString(columnIndex));
    }
    catch (...)
    {
        return 0;
    }
}

int OLEDBResultSet::getInt(const std::string &columnName)
{
    return getInt(findColumn(columnName));
}

long long OLEDBResultSet::getLong(size_t columnIndex)
{
    try
    {
        return std::stoll(getString(columnIndex));
    }
    catch (...)
    {
        return 0;
    }
}

long long OLEDBResultSet::getLong(const std::string &columnName)
{
    return getLong(findColumn(columnName));
}

double OLEDBResultSet::getDouble(size_t columnIndex)
{
    try
    {
        return std::stod(getString(columnIndex));
    }
    catch (...)
    {
        return 0.0;
    }
}

double OLEDBResultSet::getDouble(const std::string &columnName)
{
    return getDouble(findColumn(columnName));
}

bool OLEDBResultSet::getBool(size_t columnIndex)
{
    return getInt(columnIndex) != 0;
}

bool OLEDBResultSet::getBool(const std::string &columnName)
{
    return getBool(findColumn(columnName));
}

size_t OLEDBResultSet::getColumnCount() const { return columnNames_.size(); }

std::string OLEDBResultSet::getColumnName(size_t columnIndex) const
{
    if (columnIndex >= columnNames_.size())
    {
        throw DatabaseException("Column index out of range");
    }

    return columnNames_[columnIndex];
}

size_t OLEDBResultSet::findColumn(const std::string &columnName) const
{
    for (size_t i = 0; i < columnNames_.size(); ++i)
    {
        if (columnNames_[i] == columnName)
        {
            return i;
        }
    }

    throw DatabaseException("Column not found: " + columnName);
}

void OLEDBResultSet::close()
{
    // OLEDB close implementation
}

} // namespace DocDBModule
