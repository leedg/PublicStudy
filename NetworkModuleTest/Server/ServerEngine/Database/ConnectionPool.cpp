// ConnectionPool 구현

#include "ConnectionPool.h"
#include "DatabaseFactory.h"
#include <algorithm>
#include <thread>

namespace Network
{
namespace Database
{

ConnectionPool::ConnectionPool()
	: mInitialized(false), mActiveConnections(0), mMaxPoolSize(10),
		  mMinPoolSize(2), mConnectionTimeout(std::chrono::seconds(30)),
		  mIdleTimeout(std::chrono::seconds(300))
{
}

ConnectionPool::~ConnectionPool() { Shutdown(); }

bool ConnectionPool::Initialize(const DatabaseConfig &config)
{
	std::lock_guard<std::mutex> lock(mMutex);

	if (mInitialized.load())
	{
		return true;
	}

	mConfig = config;
	mMaxPoolSize = config.mMaxPoolSize;
	mMinPoolSize = config.mMinPoolSize;

	// DatabaseFactory로 백엔드 DB 인스턴스 생성
	mDatabase = DatabaseFactory::CreateDatabase(config.mType);
	if (!mDatabase)
	{
		return false;
	}

	try
	{
		mDatabase->Connect(config);

		// mMinPoolSize만큼 연결을 미리 생성하여 첫 GetConnection() 대기를 줄인다
		for (size_t i = 0; i < mMinPoolSize; ++i)
		{
			auto pConn = CreateNewConnection();
			if (pConn)
			{
				mConnections.emplace_back(pConn);
			}
		}

		mInitialized.store(true);
		return true;
	}
	catch (const DatabaseException &)
	{
		mConnections.clear();
		return false;
	}
}

void ConnectionPool::Shutdown()
{
	if (!mInitialized.load())
	{
		return;
	}

	// 모든 활성 연결이 반환될 때까지 최대 5초 대기
	{
		std::unique_lock<std::mutex> lock(mMutex);
		mCondition.wait_for(lock, std::chrono::seconds(5),
							[this] { return mActiveConnections.load() == 0; });
	}

	// 단일 락 아래 모든 연결 닫기 및 DB 연결 해제.
	// Clear() 대신 ClearLocked() 호출: 비재귀 mutex 재락킹은 UB(통상 데드락).
	// 5초 대기 타임아웃 후에도 in-use 연결이 남으면 강제 종료하여 리소스 누수 방지.
	{
		std::lock_guard<std::mutex> lock(mMutex);

		// 락 내부에서 먼저 미초기화 표시.
		// GetConnection()이 mInitialized.load() 체크를 통과한 뒤 mutex를 획득해도
		// false를 보고 즉시 반환 — 이미 클리어된 mConnections 접근을 방지.
		mInitialized.store(false);

		if (mActiveConnections.load() > 0)
		{
			// 대기 타임아웃: in-use 여부에 무관하게 남은 연결 전체를 강제 종료
			for (auto &pooled : mConnections)
			{
				pooled.mConnection->Close();
			}
			mConnections.clear();
			mActiveConnections.store(0);
		}
		else
		{
			ClearLocked();
		}

		if (mDatabase)
		{
			mDatabase->Disconnect();
			mDatabase.reset();
		}
	}
}

std::shared_ptr<IConnection> ConnectionPool::CreateNewConnection()
{
	if (!mDatabase || !mDatabase->IsConnected())
	{
		throw DatabaseException("Database not connected");
	}

	auto pConn = mDatabase->CreateConnection();
	if (!pConn)
	{
		throw DatabaseException("Failed to create connection");
	}

	pConn->Open(mConfig.mConnectionString);
	return std::shared_ptr<IConnection>(std::move(pConn));
}

std::shared_ptr<IConnection> ConnectionPool::GetConnection()
{
	if (!mInitialized.load())
	{
		throw DatabaseException("Connection pool not initialized");
	}

	std::unique_lock<std::mutex> lock(mMutex);

	// 락 획득 후 mInitialized 재확인.
	// Shutdown()이 동일 mutex 내부 첫 동작으로 false로 설정하므로,
	// 위의 선락킹 load() 체크를 통과한 스레드가 Shutdown()과 경쟁할 수 있다.
	// 재확인 없으면 wait_for 진행 후 nullptr mDatabase에서 CreateNewConnection() 호출
	// → null 포인터 역참조가 된다.
	if (!mInitialized.load(std::memory_order_relaxed))
	{
		throw DatabaseException("Connection pool is shutting down");
	}

	// 사용 가능한 연결이 생길 때까지 mConnectionTimeout 동안 대기
	bool acquired = mCondition.wait_for(
		lock, mConnectionTimeout,
		[this]
		{
			for (auto &pooled : mConnections)
			{
				if (!pooled.mInUse && pooled.mConnection->IsOpen())
				{
					return true;
				}
			}
			// 풀 상한 미만이면 새 연결 생성 가능 — 대기 해제
			return mConnections.size() < mMaxPoolSize;
		});

	if (!acquired)
	{
		throw DatabaseException(
			"Connection pool timeout - no connections available");
	}

	// 유휴 연결(mInUse=false, IsOpen=true)을 먼저 찾아 반환
	for (auto &pooled : mConnections)
	{
		if (!pooled.mInUse && pooled.mConnection->IsOpen())
		{
			pooled.mInUse = true;
			pooled.mLastUsed = std::chrono::steady_clock::now();
			mActiveConnections.fetch_add(1);
			return pooled.mConnection;
		}
	}

	// 유휴 연결 없고 풀 상한 미만이면 새 연결 생성
	if (mConnections.size() < mMaxPoolSize)
	{
		auto pConn = CreateNewConnection();
		mConnections.emplace_back(pConn);
		mConnections.back().mInUse = true;
		mActiveConnections.fetch_add(1);
		return pConn;
	}

	throw DatabaseException("No connections available");
}

void ConnectionPool::ReturnConnection(std::shared_ptr<IConnection> pConnection)
{
	if (!pConnection)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(mMutex);

	for (auto &pooled : mConnections)
	{
		if (pooled.mConnection == pConnection)
		{
			pooled.mInUse = false;
			pooled.mLastUsed = std::chrono::steady_clock::now();
			mActiveConnections.fetch_sub(1);
			mCondition.notify_one();
			return;
		}
	}
}

void ConnectionPool::ClearLocked()
{
	// 호출자가 이미 mMutex를 보유해야 한다 — 여기서 락 획득 안 함.
	for (auto it = mConnections.begin(); it != mConnections.end();)
	{
		if (!it->mInUse)
		{
			it->mConnection->Close();
			it = mConnections.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void ConnectionPool::Clear()
{
	std::lock_guard<std::mutex> lock(mMutex);
	ClearLocked();
}

size_t ConnectionPool::GetActiveConnections() const
{
	return mActiveConnections.load();
}

size_t ConnectionPool::GetAvailableConnections() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	size_t available = 0;
	for (const auto &pooled : mConnections)
	{
		if (!pooled.mInUse && pooled.mConnection->IsOpen())
		{
			++available;
		}
	}
	return available;
}

void ConnectionPool::SetMaxPoolSize(size_t size)
{
	std::lock_guard<std::mutex> lock(mMutex);
	mMaxPoolSize = size;
}

void ConnectionPool::SetMinPoolSize(size_t size)
{
	std::lock_guard<std::mutex> lock(mMutex);
	mMinPoolSize = size;
}

void ConnectionPool::SetConnectionTimeout(int seconds)
{
	mConnectionTimeout = std::chrono::seconds(seconds);
}

void ConnectionPool::SetIdleTimeout(int seconds)
{
	mIdleTimeout = std::chrono::seconds(seconds);
}

size_t ConnectionPool::GetTotalConnections() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mConnections.size();
}

void ConnectionPool::CleanupIdleConnections()
{
	std::lock_guard<std::mutex> lock(mMutex);

	auto now = std::chrono::steady_clock::now();

	for (auto it = mConnections.begin(); it != mConnections.end();)
	{
		if (!it->mInUse)
		{
			auto idleDuration =
				std::chrono::duration_cast<std::chrono::seconds>(now -
																 it->mLastUsed);

			if (idleDuration > mIdleTimeout &&
				mConnections.size() > mMinPoolSize)
			{
				it->mConnection->Close();
				it = mConnections.erase(it);
				continue;
			}
		}
		++it;
	}
}

} // namespace Database
} // namespace Network
