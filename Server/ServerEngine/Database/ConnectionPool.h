#pragma once

// English: Connection pool implementation
// 한글: 연결 풀 구현

#include "../Interfaces/DatabaseConfig.h"
#include "../Interfaces/DatabaseException.h"
#include "../Interfaces/IConnectionPool.h"
#include "../Interfaces/IDatabase.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

namespace Network
{
namespace Database
{

// =============================================================================
// English: ConnectionPool class
// 한글: ConnectionPool 클래스
// =============================================================================

/**
 * English: Connection pool implementation
 * 한글: 연결 풀 구현
 */
class ConnectionPool : public IConnectionPool
{
  public:
	ConnectionPool();
	virtual ~ConnectionPool();

	// English: Initialization
	// 한글: 초기화
	bool Initialize(const DatabaseConfig &config);
	void Shutdown();

	// English: IConnectionPool interface
	// 한글: IConnectionPool 인터페이스
	std::shared_ptr<IConnection> GetConnection() override;
	void ReturnConnection(std::shared_ptr<IConnection> pConnection) override;
	void Clear() override;
	size_t GetActiveConnections() const override;
	size_t GetAvailableConnections() const override;

	// English: Configuration
	// 한글: 설정
	void SetMaxPoolSize(size_t size);
	void SetMinPoolSize(size_t size);
	void SetConnectionTimeout(int seconds);
	void SetIdleTimeout(int seconds);

	// English: Status
	// 한글: 상태
	bool IsInitialized() const { return mInitialized.load(); }

	size_t GetTotalConnections() const;

  private:
	// English: Pooled connection structure
	// 한글: 풀링된 연결 구조체
	struct PooledConnection
	{
		std::shared_ptr<IConnection> mConnection;
		std::chrono::steady_clock::time_point mLastUsed;
		bool mInUse;

		PooledConnection(std::shared_ptr<IConnection> pConn)
			: mConnection(std::move(pConn)),
				  mLastUsed(std::chrono::steady_clock::now()), mInUse(false)
		{
		}
	};

	// English: Helper methods
	// 한글: 헬퍼 메서드
	std::shared_ptr<IConnection> CreateNewConnection();
	void CleanupIdleConnections();

	// English: ClearLocked — Close idle connections WITHOUT acquiring mMutex.
	//          Callers (Clear, Shutdown) must already hold mMutex.
	//          Prevents deadlock when Shutdown() calls Clear() while owning the lock.
	// 한글: ClearLocked — mMutex 획득 없이 유휴 연결 닫기.
	//       호출자(Clear, Shutdown)가 이미 mMutex를 보유해야 함.
	//       Shutdown()이 락 보유 상태에서 Clear()를 호출할 때 발생하는 데드락 방지.
	void ClearLocked();

  private:
	DatabaseConfig mConfig;
	std::unique_ptr<IDatabase> mDatabase;
	std::vector<PooledConnection> mConnections;
	std::mutex mMutex;
	std::condition_variable mCondition;
	std::atomic<bool> mInitialized;
	std::atomic<size_t> mActiveConnections;

	// English: Pool settings
	// 한글: 풀 설정
	size_t mMaxPoolSize;
	size_t mMinPoolSize;
	std::chrono::seconds mConnectionTimeout;
	std::chrono::seconds mIdleTimeout;
};

// =============================================================================
// English: ScopedConnection class
// 한글: ScopedConnection 클래스
// =============================================================================

/**
 * English: RAII wrapper for automatic connection return to pool
 * 한글: 풀에 자동으로 연결을 반환하는 RAII 래퍼
 */
class ScopedConnection
{
  public:
	ScopedConnection(std::shared_ptr<IConnection> pConn, IConnectionPool *pPool)
		: mConnection(std::move(pConn)), mPool(pPool)
	{
	}

	~ScopedConnection()
	{
		if (mConnection && mPool)
		{
			mPool->ReturnConnection(mConnection);
		}
	}

	// English: Prevent copy
	// 한글: 복사 방지
	ScopedConnection(const ScopedConnection &) = delete;
	ScopedConnection &operator=(const ScopedConnection &) = delete;

	// English: Allow move
	// 한글: 이동 허용
	ScopedConnection(ScopedConnection &&other) noexcept
		: mConnection(std::move(other.mConnection)), mPool(other.mPool)
	{
		other.mPool = nullptr;
	}

	// English: Access operators
	// 한글: 접근 연산자
	IConnection *operator->() { return mConnection.get(); }

	IConnection &operator*() { return *mConnection; }

	const IConnection *operator->() const { return mConnection.get(); }

	const IConnection &operator*() const { return *mConnection; }

	// English: Validation
	// 한글: 유효성 검사
	bool IsValid() const
	{
		return mConnection != nullptr && mConnection->IsOpen();
	}

	// English: Direct access
	// 한글: 직접 접근
	IConnection *Get() { return mConnection.get(); }

	const IConnection *Get() const { return mConnection.get(); }

  private:
	std::shared_ptr<IConnection> mConnection;
	IConnectionPool *mPool;
};

} // namespace Database
} // namespace Network
