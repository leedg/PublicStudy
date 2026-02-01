#pragma once

#include "../../ServerEngine/Interfaces/IConnectionPool.h"
#include "../../ServerEngine/Database/ConnectionPool.h"
#include <memory>
#include <string>

namespace TestServer {

/**
 * TestServer specific database manager
 * Manages database connection pool and provides server-specific data access methods
 *
 * Naming Convention:
 * - TestServer prefix indicates server-specific implementation
 * - DatabaseManager suffix indicates data persistence responsibility
 */
class TestServerDatabaseManager {
public:
    TestServerDatabaseManager();
    ~TestServerDatabaseManager();

    // ========================================================================
    // Lifecycle Management
    // ========================================================================

    /**
     * Initialize database connection pool with ODBC connection string
     * @param odbcConnectionString ODBC DSN connection string (e.g., "DSN=GameDB;UID=user;PWD=pass")
     * @param maxConnectionPoolSize Maximum number of concurrent database connections
     * @return true if connection pool initialized successfully
     */
    bool InitializeConnectionPool(const std::string& odbcConnectionString, int maxConnectionPoolSize = 10);

    /**
     * Gracefully shutdown database connections and cleanup resources
     */
    void ShutdownDatabase();

    /**
     * Check if database connection pool is initialized and ready for operations
     * @return true if database is ready to accept queries
     */
    bool IsDatabaseReady() const;

    // ========================================================================
    // User Management - Database Operations
    // ========================================================================

    /**
     * Persist user login event to database
     * @param userId Unique user identifier
     * @param username User's display name
     * @return true if login record saved successfully
     */
    bool SaveUserLoginEvent(uint64_t userId, const std::string& username);

    /**
     * Retrieve user profile data from database
     * @param userId Unique user identifier
     * @param outUsername Output parameter for username
     * @return true if user data found and loaded
     */
    bool LoadUserProfileData(uint64_t userId, std::string& outUsername);

    // ========================================================================
    // Game State Persistence
    // ========================================================================

    /**
     * Save player's game state to database
     * @param userId Unique user identifier
     * @param gameStateData Serialized game state (JSON, binary, etc.)
     * @return true if game state saved successfully
     */
    bool PersistPlayerGameState(uint64_t userId, const std::string& gameStateData);

    // ========================================================================
    // Custom Query Execution
    // ========================================================================

    /**
     * Execute arbitrary SQL query (use with caution)
     * @param sqlQuery SQL query string
     * @return true if query executed without errors
     */
    bool ExecuteCustomSqlQuery(const std::string& sqlQuery);

private:
    std::unique_ptr<Network::Database::ConnectionPool> mDatabaseConnectionPool;
    bool mIsInitialized;
};

} // namespace TestServer
