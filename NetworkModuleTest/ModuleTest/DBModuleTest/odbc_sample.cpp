#include "IDatabase.h"
#include <iostream>
#include <iomanip>

using namespace DocDBModule;

void printSeparator(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << " " << title << std::endl;
    std::cout << std::string(60, '=') << std::endl;
}

void printResultSet(IResultSet* resultSet) {
    if (!resultSet) {
        std::cout << "No result set returned." << std::endl;
        return;
    }
    
    // Print column headers
    size_t columnCount = resultSet->getColumnCount();
    std::vector<size_t> columnWidths(columnCount);
    
    // Calculate column widths
    for (size_t i = 0; i < columnCount; ++i) {
        columnWidths[i] = resultSet->getColumnName(i + 1).length();
    }
    
    // Find actual widths from data
    std::vector<std::vector<std::string>> rows;
    while (resultSet->next()) {
        std::vector<std::string> row;
        for (size_t i = 0; i < columnCount; ++i) {
            std::string value = resultSet->isNull(i + 1) ? "NULL" : resultSet->getString(i + 1);
            columnWidths[i] = std::max(columnWidths[i], value.length());
            row.push_back(value);
        }
        rows.push_back(row);
    }
    
    // Print headers
    for (size_t i = 0; i < columnCount; ++i) {
        std::cout << std::left << std::setw(columnWidths[i] + 2) << resultSet->getColumnName(i + 1);
    }
    std::cout << std::endl;
    
    // Print separator
    for (size_t i = 0; i < columnCount; ++i) {
        std::cout << std::string(columnWidths[i] + 2, '-');
    }
    std::cout << std::endl;
    
    // Print data rows
    for (const auto& row : rows) {
        for (size_t i = 0; i < columnCount; ++i) {
            std::cout << std::left << std::setw(columnWidths[i] + 2) << row[i];
        }
        std::cout << std::endl;
    }
    
    std::cout << "\nTotal rows: " << rows.size() << std::endl;
}

int main() {
    try {
        printSeparator("DocDBModule ODBC Sample");
        
        // Build connection string
        std::map<std::string, std::string> connParams;
        connParams["DRIVER"] = "{SQL Server}";
        connParams["SERVER"] = "localhost";
        connParams["DATABASE"] = "TestDB";
        connParams["Trusted_Connection"] = "Yes";
        
        std::string connectionString = Utils::buildODBCConnectionString(connParams);
        std::cout << "Connection String: " << connectionString << std::endl;
        
        // Create database instance
        auto database = DatabaseFactory::createODBCDatabase();
        
        // Configure database
        DatabaseConfig config;
        config.connectionString = connectionString;
        config.type = DatabaseType::ODBC;
        config.connectionTimeout = 30;
        config.commandTimeout = 30;
        config.autoCommit = true;
        
        std::cout << "\nConnecting to database..." << std::endl;
        database->connect(config);
        std::cout << "Connected successfully!" << std::endl;
        
        // Test 1: Simple query
        printSeparator("Test 1: Simple Query");
        auto statement = database->createStatement();
        statement->setQuery("SELECT @@VERSION as version");
        auto resultSet = statement->executeQuery();
        printResultSet(resultSet.get());
        
        // Test 2: Parameterized query
        printSeparator("Test 2: Parameterized Query");
        statement->setQuery("SELECT ? as test_number, ? as test_string, ? as test_date");
        statement->bindParameter(1, 42);
        statement->bindParameter(2, "Hello, ODBC!");
        statement->bindParameter(3, "2024-01-01");
        resultSet = statement->executeQuery();
        printResultSet(resultSet.get());
        
        // Test 3: Create table and insert data
        printSeparator("Test 3: Table Operations");
        statement = database->createStatement();
        
        // Drop table if exists
        try {
            statement->setQuery("DROP TABLE test_table");
            statement->executeUpdate();
            std::cout << "Dropped existing table 'test_table'" << std::endl;
        } catch (const DatabaseException& e) {
            std::cout << "Note: " << e.what() << std::endl;
        }
        
        // Create table
        statement->setQuery(R"(
            CREATE TABLE test_table (
                id INT IDENTITY(1,1) PRIMARY KEY,
                name NVARCHAR(100) NOT NULL,
                age INT,
                salary DECIMAL(10,2),
                created_date DATETIME DEFAULT GETDATE()
            )
        )");
        int result = statement->executeUpdate();
        std::cout << "Created table 'test_table'. Rows affected: " << result << std::endl;
        
        // Insert data
        statement->setQuery("INSERT INTO test_table (name, age, salary) VALUES (?, ?, ?)");
        
        std::vector<std::tuple<std::string, int, double>> employees = {
            {"John Doe", 30, 50000.50},
            {"Jane Smith", 25, 45000.75},
            {"Bob Johnson", 35, 60000.00},
            {"Alice Brown", 28, 52000.25}
        };
        
        for (const auto& emp : employees) {
            statement->bindParameter(1, std::get<0>(emp));  // name
            statement->bindParameter(2, std::get<1>(emp));  // age
            statement->bindParameter(3, std::get<2>(emp));  // salary
            result = statement->executeUpdate();
            std::cout << "Inserted: " << std::get<0>(emp) << ". Rows affected: " << result << std::endl;
        }
        
        // Query the data
        printSeparator("Test 4: Query Inserted Data");
        statement->setQuery("SELECT id, name, age, salary, created_date FROM test_table ORDER BY id");
        resultSet = statement->executeQuery();
        printResultSet(resultSet.get());
        
        // Test 5: Update operations
        printSeparator("Test 5: Update Operations");
        statement->setQuery("UPDATE test_table SET salary = salary * 1.1 WHERE age < 30");
        result = statement->executeUpdate();
        std::cout << "Updated salaries for employees under 30. Rows affected: " << result << std::endl;
        
        // Verify update
        statement->setQuery("SELECT name, age, salary FROM test_table WHERE age < 30 ORDER BY name");
        resultSet = statement->executeQuery();
        printResultSet(resultSet.get());
        
        // Test 6: Transaction test
        printSeparator("Test 6: Transaction Test");
        database->beginTransaction();
        
        try {
            statement = database->createStatement();
            statement->setQuery("INSERT INTO test_table (name, age, salary) VALUES (?, ?, ?)");
            statement->bindParameter(1, "Transaction User");
            statement->bindParameter(2, 40);
            statement->bindParameter(3, 70000.00);
            result = statement->executeUpdate();
            std::cout << "Inserted transaction record. Rows affected: " << result << std::endl;
            
            database->commitTransaction();
            std::cout << "Transaction committed successfully!" << std::endl;
        } catch (const DatabaseException& e) {
            database->rollbackTransaction();
            std::cout << "Transaction rolled back due to error: " << e.what() << std::endl;
        }
        
        // Verify transaction
        statement->setQuery("SELECT COUNT(*) as count FROM test_table WHERE name = 'Transaction User'");
        resultSet = statement->executeQuery();
        if (resultSet->next()) {
            int count = resultSet->getInt(1);
            std::cout << "Transaction user record exists: " << (count > 0 ? "YES" : "NO") << std::endl;
        }
        
        // Test 7: Stored procedure (if supported)
        printSeparator("Test 7: Stored Procedure Example");
        try {
            statement->setQuery(R"(
                CREATE PROCEDURE GetEmployeesByAge @min_age INT, @max_age INT
                AS
                BEGIN
                    SELECT id, name, age, salary 
                    FROM test_table 
                    WHERE age BETWEEN @min_age AND @max_age
                    ORDER BY age;
                END
            )");
            statement->executeUpdate();
            std::cout << "Created stored procedure 'GetEmployeesByAge'" << std::endl;
            
            // Execute stored procedure
            statement->setQuery("{CALL GetEmployeesByAge(?, ?)}");
            statement->bindParameter(1, 25);
            statement->bindParameter(2, 35);
            resultSet = statement->executeQuery();
            printResultSet(resultSet.get());
            
        } catch (const DatabaseException& e) {
            std::cout << "Stored procedure test failed: " << e.what() << std::endl;
        }
        
        // Cleanup
        printSeparator("Cleanup");
        statement = database->createStatement();
        try {
            statement->setQuery("DROP TABLE test_table");
            statement->executeUpdate();
            std::cout << "Dropped test table" << std::endl;
        } catch (const DatabaseException& e) {
            std::cout << "Cleanup failed: " << e.what() << std::endl;
        }
        
        database->disconnect();
        std::cout << "Disconnected from database." << std::endl;
        
        printSeparator("Sample Completed Successfully");
        
    } catch (const DatabaseException& e) {
        std::cerr << "Database Error: " << e.what() << " (Code: " << e.getErrorCode() << ")" << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}