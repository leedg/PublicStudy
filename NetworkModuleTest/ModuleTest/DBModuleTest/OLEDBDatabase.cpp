#include "OLEDBDatabase.h"
#include <stdexcept>

namespace DocDBModule {

	// OLEDBDatabase Implementation
OLEDBDatabase::OLEDBDatabase() : connected_(false)
{
	}

OLEDBDatabase::~OLEDBDatabase()
{
    disconnect();
}

void OLEDBDatabase::connect(const DatabaseConfig& config)
{
    config_ = config;
    connected_ = true;
}

void OLEDBDatabase::disconnect()
{
    connected_ = false;
}

bool OLEDBDatabase::isConnected() const
{
    return connected_;
}

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
OLEDBConnection::OLEDBConnection() : connected_(false), lastErrorCode_(0)
{
}

OLEDBConnection::~OLEDBConnection()
{
    close();
}

void OLEDBConnection::open([[maybe_unused]] const std::string& connectionString)
{
    if (connected_)
    {
        return; // Already connected
    }

    // OLEDB connection implementation
    connected_ = true;
}

void OLEDBConnection::close()
{
    connected_ = false;
}

bool OLEDBConnection::isOpen() const
{
    return connected_;
}

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
OLEDBStatement::OLEDBStatement() : prepared_(false), timeout_(30)
{
}

OLEDBStatement::~OLEDBStatement()
{
    close();
}

void OLEDBStatement::setQuery(const std::string& query)
{
    query_ = query;
}

void OLEDBStatement::setTimeout(int seconds)
{
    timeout_ = seconds;
}

	// Simple in-memory parameter binding for module tests
void OLEDBStatement::bindParameter(size_t index, const std::string& value)
{
    if (index == 0) return;
    if (parameters_.size() < index)
    {
        parameters_.resize(index);
    }
    parameters_[index - 1] = value;
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
    bindParameter(index, value ? "1" : "0");
}

void OLEDBStatement::bindNullParameter(size_t index)
{
    bindParameter(index, std::string());
}


std::unique_ptr<IResultSet> OLEDBStatement::executeQuery()
{
    // For module tests return an empty result set. In a full implementation this
    // would execute the query via OLE DB provider and populate results.
    prepared_ = true;
    return std::make_unique<OLEDBResultSet>();
}

int OLEDBStatement::executeUpdate()
{
    // No-op update simulation
    prepared_ = true;
    return 0;
}

bool OLEDBStatement::execute()
{
    // Execute statement without returning results
    prepared_ = true;
    return true;
}

	void OLEDBStatement::addBatch() {
		// Add current query+params to batch
		if (!query_.empty()) {
			// simple serialization: query|p1|p2|...
			std::string entry = query_;
			for (const auto& p : parameters_) {
				entry.push_back('\x1F'); // unit separator
				entry += p;
			}
			batch_.push_back(std::move(entry));
		}
	}

	std::vector<int> OLEDBStatement::executeBatch() {
		std::vector<int> results;
		for (size_t i = 0; i < batch_.size(); ++i) {
			// simulate execution success
			results.push_back(0);
		}
		batch_.clear();
		return results;
	}

	void OLEDBStatement::clearParameters() {
		parameters_.clear();
		prepared_ = false;
	}

	void OLEDBStatement::close() {
		// OLEDB close implementation
	}

	// OLEDBResultSet Implementation
	OLEDBResultSet::OLEDBResultSet() : hasData_(false), metadataLoaded_(false) {
	}

	OLEDBResultSet::~OLEDBResultSet() {
		close();
	}

	void OLEDBResultSet::loadMetadata() {
		// No metadata available in the module test stub
		metadataLoaded_ = true;
	}

	bool OLEDBResultSet::next() {
		// No rows in stub
		hasData_ = false;
		return false;
	}

	bool OLEDBResultSet::isNull([[maybe_unused]] size_t columnIndex) {
		return true;
	}

	bool OLEDBResultSet::isNull([[maybe_unused]] const std::string& columnName) {
		return true;
	}

	std::string OLEDBResultSet::getString([[maybe_unused]] size_t columnIndex) {
		return std::string();
	}

	std::string OLEDBResultSet::getString([[maybe_unused]] const std::string& columnName) {
		return std::string();
	}

	int OLEDBResultSet::getInt(size_t columnIndex) {
		try {
			return std::stoi(getString(columnIndex));
		}
		catch (...) {
			return 0;
		}
	}

	int OLEDBResultSet::getInt(const std::string& columnName) {
		try {
			return std::stoi(getString(columnName));
		}
		catch (...) {
			return 0;
		}
	}

	long long OLEDBResultSet::getLong(size_t columnIndex) {
		try {
			return std::stoll(getString(columnIndex));
		}
		catch (...) {
			return 0;
		}
	}

	long long OLEDBResultSet::getLong(const std::string& columnName) {
		try {
			return std::stoll(getString(columnName));
		}
		catch (...) {
			return 0;
		}
	}

	double OLEDBResultSet::getDouble(size_t columnIndex) {
		try {
			return std::stod(getString(columnIndex));
		}
		catch (...) {
			return 0.0;
		}
	}

	double OLEDBResultSet::getDouble(const std::string& columnName) {
		try {
			return std::stod(getString(columnName));
		}
		catch (...) {
			return 0.0;
		}
	}

	bool OLEDBResultSet::getBool(size_t columnIndex) {
		return getInt(columnIndex) != 0;
	}

	bool OLEDBResultSet::getBool(const std::string& columnName) {
		return getInt(columnName) != 0;
	}

	size_t OLEDBResultSet::getColumnCount() const {
		return columnNames_.size();
	}

	std::string OLEDBResultSet::getColumnName(size_t columnIndex) const {
		if (columnIndex >= columnNames_.size()) throw DatabaseException("Column index out of range");
		return columnNames_[columnIndex];
	}

	size_t OLEDBResultSet::findColumn(const std::string& columnName) const {
		for (size_t i = 0; i < columnNames_.size(); ++i) {
			if (columnNames_[i] == columnName) return i;
		}
		throw DatabaseException("Column not found: " + columnName);
	}

	void OLEDBResultSet::close() {
		// OLEDB close implementation
	}

} // namespace DocDBModule
