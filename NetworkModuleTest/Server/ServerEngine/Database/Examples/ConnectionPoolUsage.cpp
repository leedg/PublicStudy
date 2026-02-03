/**
 * Connection Pool Usage Example
 *
 * This file demonstrates connection pool usage including:
 * - Initializing connection pool
 * - Acquiring and returning connections
 * - Using RAII wrapper (ScopedConnection)
 * - Monitoring pool status
 */

#include "../DatabaseModule.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace Network::Database;

// Example 1: Basic connection pool usage
void basicPoolExample()
{
	std::cout << "=== Example 1: Basic Connection Pool ===" << std::endl;

	try
	{
		// Configure connection pool
		DatabaseConfig config;
		config.type = DatabaseType::ODBC;
		config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";
		config.maxPoolSize = 10;
		config.minPoolSize = 2;
		config.connectionTimeout = 30;

		ConnectionPool pool;
		if (!pool.initialize(config))
		{
			std::cerr << "Failed to initialize connection pool" << std::endl;
			return;
		}

		std::cout << "Pool initialized with " << pool.getTotalConnections()
				  << " connections" << std::endl;

		// Get connection from pool
		auto conn = pool.getConnection();
		std::cout << "Connection acquired, active: "
				  << pool.getActiveConnections() << std::endl;

		// Use connection
		auto stmt = conn->createStatement();
		stmt->setQuery("SELECT COUNT(*) FROM users");
		auto rs = stmt->executeQuery();
		if (rs->next())
		{
			std::cout << "Total users: " << rs->getInt(0) << std::endl;
		}

		// Return connection to pool
		pool.returnConnection(conn);
		std::cout << "Connection returned, active: "
				  << pool.getActiveConnections() << std::endl;

		pool.shutdown();
	}
	catch (const DatabaseException &e)
	{
		std::cerr << "Database error: " << e.what() << std::endl;
	}
}

// Example 2: Using ScopedConnection (RAII)
void scopedConnectionExample()
{
	std::cout << "\n=== Example 2: Scoped Connection ===" << std::endl;

	try
	{
		DatabaseConfig config;
		config.type = DatabaseType::ODBC;
		config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";
		config.maxPoolSize = 5;

		ConnectionPool pool;
		if (!pool.initialize(config))
		{
			std::cerr << "Failed to initialize connection pool" << std::endl;
			return;
		}

		std::cout << "Before scope: active = " << pool.getActiveConnections()
				  << std::endl;

		{
			// Connection automatically returned when scope ends
			ScopedConnection scopedConn(pool.getConnection(), &pool);

			std::cout << "Inside scope: active = "
						  << pool.getActiveConnections() << std::endl;

			auto stmt = scopedConn->createStatement();
			stmt->setQuery("SELECT * FROM users LIMIT 5");
			auto rs = stmt->executeQuery();

			while (rs->next())
			{
				std::cout << "User: " << rs->getString("name") << std::endl;
			}
		}

		std::cout << "After scope: active = " << pool.getActiveConnections()
				  << std::endl;

		pool.shutdown();
	}
	catch (const DatabaseException &e)
	{
		std::cerr << "Database error: " << e.what() << std::endl;
	}
}

// Example 3: Multi-threaded connection pool usage
void multiThreadedPoolExample()
{
	std::cout << "\n=== Example 3: Multi-threaded Pool Usage ===" << std::endl;

	try
	{
		DatabaseConfig config;
		config.type = DatabaseType::ODBC;
		config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";
		config.maxPoolSize = 5;
		config.minPoolSize = 2;

		ConnectionPool pool;
		if (!pool.initialize(config))
		{
			std::cerr << "Failed to initialize connection pool" << std::endl;
			return;
		}

		const int NUM_THREADS = 10;
		std::vector<std::thread> threads;

		auto workerFunc = [&pool](int threadId)
		{
			try
			{
				for (int i = 0; i < 3; ++i)
				{
					ScopedConnection conn(pool.getConnection(), &pool);

					std::cout << "Thread " << threadId << " acquired connection"
								  << std::endl;

					auto stmt = conn->createStatement();
					stmt->setQuery("SELECT COUNT(*) FROM users");
					auto rs = stmt->executeQuery();

					if (rs->next())
					{
						std::cout << "Thread " << threadId
								  << " got count: " << rs->getInt(0)
								  << std::endl;
					}

					// Simulate some work
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
			}
			catch (const DatabaseException &e)
			{
				std::cerr << "Thread " << threadId << " error: " << e.what()
						  << std::endl;
			}
		};

		// Create threads
		for (int i = 0; i < NUM_THREADS; ++i)
		{
			threads.emplace_back(workerFunc, i);
		}

		// Wait for all threads
		for (auto &thread : threads)
		{
			thread.join();
		}

		std::cout << "All threads completed" << std::endl;
		std::cout << "Final active connections: " << pool.getActiveConnections()
				  << std::endl;
		std::cout << "Final total connections: " << pool.getTotalConnections()
				  << std::endl;

		pool.shutdown();
	}
	catch (const DatabaseException &e)
	{
		std::cerr << "Database error: " << e.what() << std::endl;
	}
}

// Example 4: Connection pool monitoring
void poolMonitoringExample()
{
	std::cout << "\n=== Example 4: Pool Monitoring ===" << std::endl;

	try
	{
		DatabaseConfig config;
		config.type = DatabaseType::ODBC;
		config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";
		config.maxPoolSize = 5;
		config.minPoolSize = 2;

		ConnectionPool pool;
		if (!pool.initialize(config))
		{
			std::cerr << "Failed to initialize connection pool" << std::endl;
			return;
		}

		auto printPoolStatus = [&pool]()
		{
			std::cout << "Pool Status:" << std::endl;
			std::cout << "  Total: " << pool.getTotalConnections() << std::endl;
			std::cout << "  Active: " << pool.getActiveConnections()
						  << std::endl;
			std::cout << "  Available: " << pool.getAvailableConnections()
						  << std::endl;
			std::cout << "  Initialized: "
						  << (pool.isInitialized() ? "Yes" : "No") << std::endl;
		};

		std::cout << "Initial state:" << std::endl;
		printPoolStatus();

		// Acquire multiple connections
		std::vector<std::shared_ptr<IConnection>> connections;
		for (int i = 0; i < 3; ++i)
		{
			connections.push_back(pool.getConnection());
			std::cout << "\nAfter acquiring connection " << (i + 1) << ":"
						  << std::endl;
			printPoolStatus();
		}

		// Return connections
		for (size_t i = 0; i < connections.size(); ++i)
		{
			pool.returnConnection(connections[i]);
			std::cout << "\nAfter returning connection " << (i + 1) << ":"
						  << std::endl;
			printPoolStatus();
		}

		pool.shutdown();
	}
	catch (const DatabaseException &e)
	{
		std::cerr << "Database error: " << e.what() << std::endl;
	}
}

// Example 5: Pool configuration and tuning
void poolConfigurationExample()
{
	std::cout << "\n=== Example 5: Pool Configuration ===" << std::endl;

	try
	{
		DatabaseConfig config;
		config.type = DatabaseType::ODBC;
		config.connectionString = "DSN=MyDatabase;UID=user;PWD=password";
		config.maxPoolSize = 20;
		config.minPoolSize = 5;
		config.connectionTimeout = 60;

		ConnectionPool pool;
		if (!pool.initialize(config))
		{
			std::cerr << "Failed to initialize connection pool" << std::endl;
			return;
		}

		std::cout << "Initial configuration:" << std::endl;
		std::cout << "  Max pool size: 20" << std::endl;
		std::cout << "  Min pool size: 5" << std::endl;

		// Adjust pool settings
		pool.setMaxPoolSize(15);
		pool.setMinPoolSize(3);
		pool.setConnectionTimeout(30);
		pool.setIdleTimeout(300);

		std::cout << "\nAdjusted configuration:" << std::endl;
		std::cout << "  Max pool size: 15" << std::endl;
		std::cout << "  Min pool size: 3" << std::endl;

		// Test with adjusted settings
		ScopedConnection conn(pool.getConnection(), &pool);
		auto stmt = conn->createStatement();
		stmt->setQuery("SELECT 1");
		auto rs = stmt->executeQuery();

		std::cout << "Query executed successfully with new settings"
				  << std::endl;

		pool.shutdown();
	}
	catch (const DatabaseException &e)
	{
		std::cerr << "Database error: " << e.what() << std::endl;
	}
}

int main()
{
	std::cout << "Database Module Connection Pool Examples" << std::endl;
	std::cout << "Version: " << ModuleVersion::VERSION_STRING << std::endl;
	std::cout << std::endl;

	// Run examples (comment out if database is not available)
	// basicPoolExample();
	// scopedConnectionExample();
	// multiThreadedPoolExample();
	// poolMonitoringExample();
	// poolConfigurationExample();

	std::cout << "\nNote: Uncomment examples to run with actual database"
				  << std::endl;

	return 0;
}
