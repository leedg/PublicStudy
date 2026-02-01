#include "../include/TestDatabaseManager.h"
#include "../../ServerEngine/Database/DatabaseFactory.h"
#include "../../ServerEngine/Database/DatabaseModule.h"
#include <iostream>

namespace TestServer {

TestServerDatabaseManager::TestServerDatabaseManager()
    : mIsInitialized(false)
{
}

TestServerDatabaseManager::~TestServerDatabaseManager() {
    ShutdownDatabase();
}

bool TestServerDatabaseManager::InitializeConnectionPool(const std::string& connectionString, int maxPoolSize) {
    if (mIsInitialized) {
        std::cout << "[TestServerDatabaseManager] Already initialized" << std::endl;
        return true;
    }

    try {
        // Configure database
        Network::Database::DatabaseConfig config;
        config.type = Network::Database::DatabaseType::ODBC;
        config.connectionString = connectionString;
        config.maxPoolSize = maxPoolSize;
        config.minPoolSize = 2;
        config.connectionTimeout = 30;

        // Create connection pool
        mDatabaseConnectionPool = std::make_unique<Network::Database::ConnectionPool>();

        if (!mDatabaseConnectionPool->initialize(config)) {
            std::cerr << "[TestServerDatabaseManager] Failed to initialize connection pool" << std::endl;
            return false;
        }

        mIsInitialized = true;
        std::cout << "[TestServerDatabaseManager] Initialized successfully" << std::endl;
        std::cout << "[TestServerDatabaseManager] Pool size: " << maxPoolSize << std::endl;

        return true;
    }
    catch (const Network::Database::DatabaseException& e) {
        std::cerr << "[TestServerDatabaseManager] Initialization error: " << e.what() << std::endl;
        return false;
    }
}

void TestServerDatabaseManager::ShutdownDatabase() {
    if (mDatabaseConnectionPool) {
        mDatabaseConnectionPool->shutdown();
        mDatabaseConnectionPool.reset();
    }
    mIsInitialized = false;
    std::cout << "[TestServerDatabaseManager] Shutdown complete" << std::endl;
}

bool TestServerDatabaseManager::IsDatabaseReady() const {
    return mIsInitialized && mDatabaseConnectionPool && mDatabaseConnectionPool->isInitialized();
}

bool TestServerDatabaseManager::SaveUserLoginEvent(uint64_t userId, const std::string& username) {
    if (!IsDatabaseReady()) {
        std::cerr << "[TestServerDatabaseManager] Database not ready" << std::endl;
        return false;
    }

    try {
        auto conn = mDatabaseConnectionPool->getConnection();
        auto stmt = conn->createStatement();

        stmt->setQuery("INSERT INTO user_logins (user_id, username, login_time) VALUES (?, ?, CURRENT_TIMESTAMP)");
        stmt->bindParameter(1, static_cast<long long>(userId));
        stmt->bindParameter(2, username);

        int rowsAffected = stmt->executeUpdate();

        mDatabaseConnectionPool->returnConnection(conn);

        std::cout << "[TestServerDatabaseManager] User login saved: " << username
                  << " (rows: " << rowsAffected << ")" << std::endl;

        return rowsAffected > 0;
    }
    catch (const Network::Database::DatabaseException& e) {
        std::cerr << "[TestServerDatabaseManager] SaveUserLogin error: " << e.what() << std::endl;
        return false;
    }
}

bool TestServerDatabaseManager::LoadUserProfileData(uint64_t userId, std::string& outUsername) {
    if (!IsDatabaseReady()) {
        std::cerr << "[TestServerDatabaseManager] Database not ready" << std::endl;
        return false;
    }

    try {
        auto conn = mDatabaseConnectionPool->getConnection();
        auto stmt = conn->createStatement();

        stmt->setQuery("SELECT username FROM users WHERE user_id = ?");
        stmt->bindParameter(1, static_cast<long long>(userId));

        auto rs = stmt->executeQuery();

        bool found = false;
        if (rs->next()) {
            outUsername = rs->getString("username");
            found = true;
        }

        mDatabaseConnectionPool->returnConnection(conn);

        if (found) {
            std::cout << "[TestServerDatabaseManager] User data loaded: " << outUsername << std::endl;
        }

        return found;
    }
    catch (const Network::Database::DatabaseException& e) {
        std::cerr << "[TestServerDatabaseManager] LoadUserData error: " << e.what() << std::endl;
        return false;
    }
}

bool TestServerDatabaseManager::PersistPlayerGameState(uint64_t userId, const std::string& stateData) {
    if (!IsDatabaseReady()) {
        std::cerr << "[TestServerDatabaseManager] Database not ready" << std::endl;
        return false;
    }

    try {
        auto conn = mDatabaseConnectionPool->getConnection();
        auto stmt = conn->createStatement();

        stmt->setQuery("UPDATE game_states SET state_data = ?, updated_at = CURRENT_TIMESTAMP WHERE user_id = ?");
        stmt->bindParameter(1, stateData);
        stmt->bindParameter(2, static_cast<long long>(userId));

        int rowsAffected = stmt->executeUpdate();

        mDatabaseConnectionPool->returnConnection(conn);

        std::cout << "[TestServerDatabaseManager] Game state saved for user " << userId << std::endl;

        return rowsAffected > 0;
    }
    catch (const Network::Database::DatabaseException& e) {
        std::cerr << "[TestServerDatabaseManager] SaveGameState error: " << e.what() << std::endl;
        return false;
    }
}

bool TestServerDatabaseManager::ExecuteCustomSqlQuery(const std::string& query) {
    if (!IsDatabaseReady()) {
        std::cerr << "[TestServerDatabaseManager] Database not ready" << std::endl;
        return false;
    }

    try {
        auto conn = mDatabaseConnectionPool->getConnection();
        auto stmt = conn->createStatement();

        stmt->setQuery(query);
        bool result = stmt->execute();

        mDatabaseConnectionPool->returnConnection(conn);

        std::cout << "[TestServerDatabaseManager] Query executed: " << query << std::endl;

        return result;
    }
    catch (const Network::Database::DatabaseException& e) {
        std::cerr << "[TestServerDatabaseManager] ExecuteQuery error: " << e.what() << std::endl;
        return false;
    }
}

} // namespace TestServer
