#pragma once

// 연결 풀(connection pool) 구현.
//
// Thread-safety 보장 방식:
//   - mMutex(std::mutex) + mCondition(std::condition_variable)으로
//     GetConnection() 대기 및 ReturnConnection() 알림을 처리한다.
//   - mInitialized, mActiveConnections는 std::atomic으로 선언되어
//     락 없이 빠른 상태 확인(GetConnection() 진입 전 체크)이 가능하다.
//   - GetConnection() 내부에서 mInitialized를 락 획득 후 재확인하여
//     Shutdown()과의 경쟁 조건(null dereference)을 방지한다.
//   - ClearLocked()는 이미 mMutex를 보유한 호출자(Shutdown 등)만 사용하여
//     비재귀 mutex의 재진입으로 인한 데드락을 원천 차단한다.
//
// 풀 크기 기본값 (DatabaseConfig에서 오버라이드 가능):
//   - mMaxPoolSize = 10: 동시 active 연결 상한. 초과 시 타임아웃까지 블록.
//   - mMinPoolSize = 2: 초기화 시 미리 생성하는 연결 수 (워밍업).
//   - mConnectionTimeout = 30초: GetConnection() 대기 최대 시간.
//   - mIdleTimeout = 300초: 유휴 연결 회수 기준 시간 (mMinPoolSize 미만으로는 회수 안 함).

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
// ConnectionPool — IConnectionPool의 스레드 안전 구현
// =============================================================================

class ConnectionPool : public IConnectionPool
{
  public:
	ConnectionPool();
	virtual ~ConnectionPool();

	// Initialize: DatabaseFactory로 백엔드 DB 생성, mMinPoolSize만큼 연결 미리 생성.
	// 반환값 false이면 DB 연결 실패 (예외 대신 bool 반환 — 초기화 실패는 복구 가능).
	bool Initialize(const DatabaseConfig &config);

	// Shutdown: 모든 연결을 반환받을 때까지 최대 5초 대기 후 강제 종료.
	//           Shutdown 후 GetConnection() 호출 시 DatabaseException 발생.
	void Shutdown();

	// IConnectionPool 인터페이스
	std::shared_ptr<IConnection> GetConnection() override;
	void ReturnConnection(std::shared_ptr<IConnection> pConnection) override;
	void Clear() override;
	size_t GetActiveConnections() const override;
	size_t GetAvailableConnections() const override;

	// 런타임 풀 크기 조정 (락으로 보호)
	void SetMaxPoolSize(size_t size);
	void SetMinPoolSize(size_t size);
	// 타임아웃은 atomic 없이 단순 대입 (조정 시 동시 GetConnection 없다고 가정)
	void SetConnectionTimeout(int seconds);
	void SetIdleTimeout(int seconds);

	bool IsInitialized() const { return mInitialized.load(); }

	size_t GetTotalConnections() const;

  private:
	// ClearLocked — 호출자가 이미 mMutex를 보유한 상태에서만 호출.
	// 비재귀 mutex 재진입으로 인한 데드락을 방지하기 위해 Clear()와 분리.
	void ClearLocked();

	// 풀링된 연결 항목 — 연결 객체, 마지막 사용 시각, 사용 중 여부를 묶음
	struct PooledConnection
	{
		std::shared_ptr<IConnection> mConnection;              // 풀링된 연결 인스턴스
		std::chrono::steady_clock::time_point mLastUsed;       // 마지막으로 GetConnection/ReturnConnection된 시각 (아이들 타임아웃 기준)
		bool mInUse;                                           // true이면 현재 대여 중, false이면 유휴

		PooledConnection(std::shared_ptr<IConnection> pConn)
			: mConnection(std::move(pConn)),
				  mLastUsed(std::chrono::steady_clock::now()), mInUse(false)
		{
		}
	};

	std::shared_ptr<IConnection> CreateNewConnection();
	void CleanupIdleConnections();

  private:
	DatabaseConfig mConfig;                          // Initialize() 시 전달된 설정 복사본
	std::unique_ptr<IDatabase> mDatabase;            // 백엔드 DB 인스턴스 (DatabaseFactory로 생성) — 연결 팩토리 역할
	std::vector<PooledConnection> mConnections;      // 풀에 등록된 모든 연결 항목 목록 (mMutex로 보호)
	mutable std::mutex mMutex;                       // mConnections 접근 및 condition wait/notify 보호
	std::condition_variable mCondition;              // GetConnection() 대기 및 ReturnConnection() 알림
	std::atomic<bool> mInitialized;                  // Initialize() 성공 후 true; Shutdown() 시 false
	std::atomic<size_t> mActiveConnections;          // 현재 대여 중(mInUse=true)인 연결 수 (락 없이 조회 가능)

	// 풀 설정값 — 기본값은 생성자에서 설정, DatabaseConfig 또는 Set*()로 변경 가능
	size_t mMaxPoolSize;                             // 동시 active 연결 상한 (기본 10)
	size_t mMinPoolSize;                             // 초기 미리 생성 연결 수 (기본 2)
	std::chrono::seconds mConnectionTimeout;         // GetConnection() 최대 대기 시간 (기본 30초)
	std::chrono::seconds mIdleTimeout;               // 유휴 연결 회수 기준 경과 시간 (기본 300초)
};

// =============================================================================
// ScopedConnection — RAII 방식으로 풀 반환을 자동화하는 연결 래퍼.
// 스코프를 벗어나면 소멸자에서 ReturnConnection()을 자동 호출한다.
// pool.GetConnection()이 nullptr를 반환할 수 있으므로 IsValid()로 확인 후 사용.
// =============================================================================

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

	// 복사 불가 (연결 소유권 이중 반환 방지)
	ScopedConnection(const ScopedConnection &) = delete;
	ScopedConnection &operator=(const ScopedConnection &) = delete;

	// 이동 허용 (소유권 이전)
	ScopedConnection(ScopedConnection &&other) noexcept
		: mConnection(std::move(other.mConnection)), mPool(other.mPool)
	{
		other.mPool = nullptr;
	}

	// 연결 접근 연산자
	IConnection *operator->() { return mConnection.get(); }

	IConnection &operator*() { return *mConnection; }

	const IConnection *operator->() const { return mConnection.get(); }

	const IConnection &operator*() const { return *mConnection; }

	// 연결이 유효하고 열려 있는지 확인
	// GetConnection()이 nullptr를 반환한 경우에도 안전하게 체크 가능.
	bool IsValid() const
	{
		return mConnection != nullptr && mConnection->IsOpen();
	}

	// 원시 포인터 직접 접근
	IConnection *Get() { return mConnection.get(); }

	const IConnection *Get() const { return mConnection.get(); }

  private:
	std::shared_ptr<IConnection> mConnection;  // 풀에서 대여한 연결; 소멸자에서 ReturnConnection으로 반환
	IConnectionPool *mPool;                    // 연결을 반환할 풀 포인터 (이동 후 nullptr)
};

} // namespace Database
} // namespace Network
