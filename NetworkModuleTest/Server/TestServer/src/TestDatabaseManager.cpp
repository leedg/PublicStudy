// English: TestDatabaseManager implementation
// 한글: TestDatabaseManager 구현

#include "../include/TestDatabaseManager.h"
#include "../../ServerEngine/Database/DatabaseFactory.h"
#include "../../ServerEngine/Database/DatabaseModule.h"
#include <iostream>

namespace TestServer
{

	TestServerDatabaseManager::TestServerDatabaseManager()
		: mIsInitialized(false)
	{
	}

	TestServerDatabaseManager::~TestServerDatabaseManager()
	{
		ShutdownDatabase();
	}

	bool TestServerDatabaseManager::InitializeConnectionPool(const std::string& connectionString, int maxPoolSize)
	{
		if (mIsInitialized)
		{
			std::cout << "[TestServerDatabaseManager] Already initialized" << std::endl;
			return true;
		}

		try
		{
			// English: Configure database
			// 한글: 데이터베이스 설정
			Network::Database::DatabaseConfig config;
			config.mType = Network::Database::DatabaseType::ODBC;
			config.mConnectionString = connectionString;
			config.mMaxPoolSize = maxPoolSize;
			config.mMinPoolSize = 2;
			config.mConnectionTimeout = 30;

			// English: Create connection pool
			// 한글: 연결 풀 생성
			mDatabaseConnectionPool = std::make_unique<Network::Database::ConnectionPool>();

			if (!mDatabaseConnectionPool->Initialize(config))
			{
				std::cerr << "[TestServerDatabaseManager] Failed to initialize connection pool" << std::endl;
				return false;
			}

			mIsInitialized = true;
			std::cout << "[TestServerDatabaseManager] Initialized successfully" << std::endl;
			std::cout << "[TestServerDatabaseManager] Pool size: " << maxPoolSize << std::endl;

			return true;
		}
		catch (const Network::Database::DatabaseException& e)
		{
			std::cerr << "[TestServerDatabaseManager] Initialization error: " << e.what() << std::endl;
			return false;
		}
	}

	void TestServerDatabaseManager::ShutdownDatabase()
	{
		if (mDatabaseConnectionPool)
		{
			mDatabaseConnectionPool->Shutdown();
			mDatabaseConnectionPool.reset();
		}
		mIsInitialized = false;
		std::cout << "[TestServerDatabaseManager] Shutdown complete" << std::endl;
	}

	bool TestServerDatabaseManager::IsDatabaseReady() const
	{
		return mIsInitialized && mDatabaseConnectionPool && mDatabaseConnectionPool->IsInitialized();
	}

	bool TestServerDatabaseManager::SaveUserLoginEvent(uint64_t userId, const std::string& username)
	{
		if (!IsDatabaseReady())
		{
			std::cerr << "[TestServerDatabaseManager] Database not ready" << std::endl;
			return false;
		}

		try
		{
			auto pConn = mDatabaseConnectionPool->GetConnection();
			auto pStmt = pConn->CreateStatement();

			pStmt->SetQuery("INSERT INTO user_logins (user_id, username, login_time) VALUES (?, ?, CURRENT_TIMESTAMP)");
			pStmt->BindParameter(1, static_cast<long long>(userId));
			pStmt->BindParameter(2, username);

			int rowsAffected = pStmt->ExecuteUpdate();

			mDatabaseConnectionPool->ReturnConnection(pConn);

			std::cout << "[TestServerDatabaseManager] User login saved: " << username
				<< " (rows: " << rowsAffected << ")" << std::endl;

			return rowsAffected > 0;
		}
		catch (const Network::Database::DatabaseException& e)
		{
			std::cerr << "[TestServerDatabaseManager] SaveUserLogin error: " << e.what() << std::endl;
			return false;
		}
	}

	bool TestServerDatabaseManager::LoadUserProfileData(uint64_t userId, std::string& outUsername)
	{
		if (!IsDatabaseReady())
		{
			std::cerr << "[TestServerDatabaseManager] Database not ready" << std::endl;
			return false;
		}

		try
		{
			auto pConn = mDatabaseConnectionPool->GetConnection();
			auto pStmt = pConn->CreateStatement();

			pStmt->SetQuery("SELECT username FROM users WHERE user_id = ?");
			pStmt->BindParameter(1, static_cast<long long>(userId));

			auto pRs = pStmt->ExecuteQuery();

			bool found = false;
			if (pRs->Next())
			{
				outUsername = pRs->GetString("username");
				found = true;
			}

			mDatabaseConnectionPool->ReturnConnection(pConn);

			if (found)
			{
				std::cout << "[TestServerDatabaseManager] User data loaded: " << outUsername << std::endl;
			}

			return found;
		}
		catch (const Network::Database::DatabaseException& e)
		{
			std::cerr << "[TestServerDatabaseManager] LoadUserData error: " << e.what() << std::endl;
			return false;
		}
	}

	bool TestServerDatabaseManager::PersistPlayerGameState(uint64_t userId, const std::string& stateData)
	{
		if (!IsDatabaseReady())
		{
			std::cerr << "[TestServerDatabaseManager] Database not ready" << std::endl;
			return false;
		}

		try
		{
			auto pConn = mDatabaseConnectionPool->GetConnection();
			auto pStmt = pConn->CreateStatement();

			pStmt->SetQuery("UPDATE game_states SET state_data = ?, updated_at = CURRENT_TIMESTAMP WHERE user_id = ?");
			pStmt->BindParameter(1, stateData);
			pStmt->BindParameter(2, static_cast<long long>(userId));

			int rowsAffected = pStmt->ExecuteUpdate();

			mDatabaseConnectionPool->ReturnConnection(pConn);

			std::cout << "[TestServerDatabaseManager] Game state saved for user " << userId << std::endl;

			return rowsAffected > 0;
		}
		catch (const Network::Database::DatabaseException& e)
		{
			std::cerr << "[TestServerDatabaseManager] SaveGameState error: " << e.what() << std::endl;
			return false;
		}
	}

	bool TestServerDatabaseManager::ExecuteCustomSqlQuery(const std::string& query)
	{
		if (!IsDatabaseReady())
		{
			std::cerr << "[TestServerDatabaseManager] Database not ready" << std::endl;
			return false;
		}

		try
		{
			auto pConn = mDatabaseConnectionPool->GetConnection();
			auto pStmt = pConn->CreateStatement();

			pStmt->SetQuery(query);
			bool result = pStmt->Execute();

			mDatabaseConnectionPool->ReturnConnection(pConn);

			std::cout << "[TestServerDatabaseManager] Query executed: " << query << std::endl;

			return result;
		}
		catch (const Network::Database::DatabaseException& e)
		{
			std::cerr << "[TestServerDatabaseManager] ExecuteQuery error: " << e.what() << std::endl;
			return false;
		}
	}

}  // namespace TestServer
