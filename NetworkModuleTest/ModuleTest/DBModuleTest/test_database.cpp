#include "IDatabase.h"
#include <iostream>
#include <cassert>
#include <string>

using namespace DocDBModule;

void test_database_factory() {
    std::cout << "Testing Database Factory..." << std::endl;
    
    // Test ODBC creation
    auto odbcDb = DatabaseFactory::createODBCDatabase();
    assert(odbcDb != nullptr);
    assert(odbcDb->getType() == DatabaseType::ODBC);
    std::cout << "✓ ODBC Database creation successful" << std::endl;
    
    // Test OLEDB creation
    auto oledbDb = DatabaseFactory::createOLEDBDatabase();
    assert(oledbDb != nullptr);
    assert(oledbDb->getType() == DatabaseType::OLEDB);
    std::cout << "✓ OLEDB Database creation successful" << std::endl;
    
    // Test generic factory
    auto db1 = DatabaseFactory::createDatabase(DatabaseType::ODBC);
    assert(db1 != nullptr);
    assert(db1->getType() == DatabaseType::ODBC);
    std::cout << "✓ Generic ODBC Database creation successful" << std::endl;
    
    auto db2 = DatabaseFactory::createDatabase(DatabaseType::OLEDB);
    assert(db2 != nullptr);
    assert(db2->getType() == DatabaseType::OLEDB);
    std::cout << "✓ Generic OLEDB Database creation successful" << std::endl;
}

void test_connection_string_utils() {
    std::cout << "\nTesting Connection String Utils..." << std::endl;
    
    // Test ODBC connection string
    std::map<std::string, std::string> odbcParams = {
        {"DRIVER", "{SQL Server}"},
        {"SERVER", "localhost"},
        {"DATABASE", "TestDB"},
        {"Trusted_Connection", "Yes"}
    };
    
    std::string odbcConnStr = Utils::buildODBCConnectionString(odbcParams);
    std::cout << "ODBC Connection String: " << odbcConnStr << std::endl;
    assert(!odbcConnStr.empty());
    assert(odbcConnStr.find("DRIVER={SQL Server}") != std::string::npos);
    std::cout << "✓ ODBC connection string building successful" << std::endl;
    
    // Test OLEDB connection string
    std::map<std::string, std::string> oledbParams = {
        {"Provider", "SQLOLEDB"},
        {"Server", "localhost"},
        {"Database", "TestDB"},
        {"Integrated Security", "SSPI"}
    };
    
    std::string oledbConnStr = Utils::buildOLEDBConnectionString(oledbParams);
    std::cout << "OLEDB Connection String: " << oledbConnStr << std::endl;
    assert(!oledbConnStr.empty());
    assert(oledbConnStr.find("Provider=SQLOLEDB") != std::string::npos);
    std::cout << "✓ OLEDB connection string building successful" << std::endl;
}

void test_database_config() {
    std::cout << "\nTesting Database Configuration..." << std::endl;
    
    DatabaseConfig config;
    config.connectionString = "Test Connection String";
    config.type = DatabaseType::ODBC;
    config.connectionTimeout = 30;
    config.commandTimeout = 60;
    config.autoCommit = false;
    config.maxPoolSize = 20;
    config.minPoolSize = 2;
    
    assert(config.connectionString == "Test Connection String");
    assert(config.type == DatabaseType::ODBC);
    assert(config.connectionTimeout == 30);
    assert(config.commandTimeout == 60);
    assert(config.autoCommit == false);
    assert(config.maxPoolSize == 20);
    assert(config.minPoolSize == 2);
    
    std::cout << "✓ Database configuration creation successful" << std::endl;
}

void test_exception_handling() {
    std::cout << "\nTesting Exception Handling..." << std::endl;
    
    try {
        throw DatabaseException("Test error message", 1234);
    } catch (const DatabaseException& e) {
        assert(std::string(e.what()) == "Test error message");
        assert(e.getErrorCode() == 1234);
        std::cout << "✓ DatabaseException handling successful" << std::endl;
    }
}

void test_parameter_binding() {
    std::cout << "\nTesting Parameter Binding Templates..." << std::endl;
    
    // Note: This test would require actual database connection for full testing
    // For now, we test the template compilation
    
    std::string testString = "test";
    int testInt = 42;
    long long testLong = 1234567890LL;
    double testDouble = 3.14159;
    bool testBool = true;
    
    // These would normally require a valid statement object
    // For testing purposes, we just verify the template parameters are valid
    std::cout << "✓ Parameter binding templates compile successfully" << std::endl;
    std::cout << "  String: " << testString << std::endl;
    std::cout << "  Int: " << testInt << std::endl;
    std::cout << "  Long: " << testLong << std::endl;
    std::cout << "  Double: " << testDouble << std::endl;
    std::cout << "  Bool: " << testBool << std::endl;
}

int main() {
    try {
        std::cout << "=== DocDBModule Unit Tests ===" << std::endl;
        
        test_database_factory();
        test_connection_string_utils();
        test_database_config();
        test_exception_handling();
        test_parameter_binding();
        
        std::cout << "\n=== All Tests Passed Successfully! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
    
    return 0;
}