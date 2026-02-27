/**
 * Basic Usage Example for Database Module
 *
 * This file demonstrates basic usage of the Database Module including:
 * - Creating a database connection
 * - Executing queries
 * - Using prepared statements
 * - Handling results
 */

#include "../DatabaseModule.h"
#include <iostream>
#include <string>

using namespace Network::Database;

// Example 1: Basic database connection and query
void basicQueryExample()
{
	std::cout << "=== Example 1: Basic Query ===" << std::endl;

	try
	{
		// Configure database
		DatabaseConfig config;
		config.type = DatabaseType::ODBC;
		config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";
		config.connectionTimeout = 30;

		// Create database instance
		auto db = DatabaseFactory::createDatabase(config.type);
		db->connect(config);

		// Create statement and execute query
		auto stmt = db->createStatement();
		stmt->setQuery("SELECT id, name, age FROM users");

		auto rs = stmt->executeQuery();
		while (rs->next())
		{
			int id = rs->getInt("id");
			std::string name = rs->getString("name");
			int age = rs->getInt("age");

			std::cout << "ID: " << id << ", Name: " << name << ", Age: " << age
						  << std::endl;
		}

		db->disconnect();
		std::cout << "Query executed successfully" << std::endl;
	}
	catch (const DatabaseException &e)
	{
		std::cerr << "Database error: " << e.what() << std::endl;
	}
}

// Example 2: Prepared statement with parameters
void preparedStatementExample()
{
	std::cout << "\n=== Example 2: Prepared Statement ===" << std::endl;

	try
	{
		DatabaseConfig config;
		config.type = DatabaseType::ODBC;
		config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";

		auto db = DatabaseFactory::createDatabase(config.type);
		db->connect(config);

		auto conn = db->createConnection();
		conn->open(config.connectionString);

		auto stmt = conn->createStatement();
		stmt->setQuery("SELECT * FROM users WHERE age > ? AND name LIKE ?");

		// Bind parameters
		stmt->bindParameter(1, 25);
		stmt->bindParameter(2, "John%");

		auto rs = stmt->executeQuery();
		while (rs->next())
		{
			std::cout << "Found: " << rs->getString("name") << std::endl;
		}

		conn->close();
		db->disconnect();
	}
	catch (const DatabaseException &e)
	{
		std::cerr << "Database error: " << e.what() << std::endl;
	}
}

// Example 3: Insert/Update/Delete operations
void modifyDataExample()
{
	std::cout << "\n=== Example 3: Modify Data ===" << std::endl;

	try
	{
		DatabaseConfig config;
		config.type = DatabaseType::ODBC;
		config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";

		auto db = DatabaseFactory::createDatabase(config.type);
		db->connect(config);

		auto conn = db->createConnection();
		conn->open(config.connectionString);

		// Insert
		auto insertStmt = conn->createStatement();
		insertStmt->setQuery("INSERT INTO users (name, age) VALUES (?, ?)");
		insertStmt->bindParameter(1, "Alice");
		insertStmt->bindParameter(2, 30);
		int rowsInserted = insertStmt->executeUpdate();
		std::cout << "Inserted " << rowsInserted << " row(s)" << std::endl;

		// Update
		auto updateStmt = conn->createStatement();
		updateStmt->setQuery("UPDATE users SET age = ? WHERE name = ?");
		updateStmt->bindParameter(1, 31);
		updateStmt->bindParameter(2, "Alice");
		int rowsUpdated = updateStmt->executeUpdate();
		std::cout << "Updated " << rowsUpdated << " row(s)" << std::endl;

		// Delete
		auto deleteStmt = conn->createStatement();
		deleteStmt->setQuery("DELETE FROM users WHERE name = ?");
		deleteStmt->bindParameter(1, "Alice");
		int rowsDeleted = deleteStmt->executeUpdate();
		std::cout << "Deleted " << rowsDeleted << " row(s)" << std::endl;

		conn->close();
		db->disconnect();
	}
	catch (const DatabaseException &e)
	{
		std::cerr << "Database error: " << e.what() << std::endl;
	}
}

// Example 4: Transaction management
void transactionExample()
{
	std::cout << "\n=== Example 4: Transactions ===" << std::endl;

	try
	{
		DatabaseConfig config;
		config.type = DatabaseType::ODBC;
		config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";
		config.autoCommit = false;

		auto db = DatabaseFactory::createDatabase(config.type);
		db->connect(config);

		auto conn = db->createConnection();
		conn->open(config.connectionString);

		try
		{
			// Begin transaction
			conn->beginTransaction();

			// Execute multiple operations
			auto stmt1 = conn->createStatement();
			stmt1->setQuery(
				"INSERT INTO accounts (name, balance) VALUES (?, ?)");
			stmt1->bindParameter(1, "Account A");
			stmt1->bindParameter(2, 1000.0);
			stmt1->executeUpdate();

			auto stmt2 = conn->createStatement();
			stmt2->setQuery(
				"INSERT INTO accounts (name, balance) VALUES (?, ?)");
			stmt2->bindParameter(1, "Account B");
			stmt2->bindParameter(2, 2000.0);
			stmt2->executeUpdate();

			// Commit transaction
			conn->commitTransaction();
			std::cout << "Transaction committed successfully" << std::endl;
		}
		catch (const DatabaseException &e)
		{
			// Rollback on error
			conn->rollbackTransaction();
			std::cerr << "Transaction rolled back: " << e.what() << std::endl;
		}

		conn->close();
		db->disconnect();
	}
	catch (const DatabaseException &e)
	{
		std::cerr << "Database error: " << e.what() << std::endl;
	}
}

// Example 5: Batch operations
void batchOperationExample()
{
	std::cout << "\n=== Example 5: Batch Operations ===" << std::endl;

	try
	{
		DatabaseConfig config;
		config.type = DatabaseType::ODBC;
		config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";

		auto db = DatabaseFactory::createDatabase(config.type);
		db->connect(config);

		auto conn = db->createConnection();
		conn->open(config.connectionString);

		auto stmt = conn->createStatement();
		stmt->setQuery("INSERT INTO users (name, age) VALUES (?, ?)");

		// Add multiple batches
		stmt->bindParameter(1, "User1");
		stmt->bindParameter(2, 20);
		stmt->addBatch();

		stmt->clearParameters();
		stmt->bindParameter(1, "User2");
		stmt->bindParameter(2, 25);
		stmt->addBatch();

		stmt->clearParameters();
		stmt->bindParameter(1, "User3");
		stmt->bindParameter(2, 30);
		stmt->addBatch();

		// Execute all batches
		auto results = stmt->executeBatch();
		std::cout << "Batch executed, " << results.size()
				  << " operations completed" << std::endl;

		conn->close();
		db->disconnect();
	}
	catch (const DatabaseException &e)
	{
		std::cerr << "Database error: " << e.what() << std::endl;
	}
}

int main()
{
	std::cout << "Database Module Basic Usage Examples" << std::endl;
	std::cout << "Version: " << ModuleVersion::VERSION_STRING << std::endl;
	std::cout << "Build Date: " << ModuleVersion::BUILD_DATE << std::endl;
	std::cout << std::endl;

	// Run examples (comment out if database is not available)
	// basicQueryExample();
	// preparedStatementExample();
	// modifyDataExample();
	// transactionExample();
	// batchOperationExample();

	std::cout << "\nNote: Uncomment examples to run with actual database"
				  << std::endl;

	return 0;
}
