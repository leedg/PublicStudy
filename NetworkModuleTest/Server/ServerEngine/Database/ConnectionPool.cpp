// English: ConnectionPool implementation
// 한글: ConnectionPool 구현

#include "ConnectionPool.h"
#include "DatabaseFactory.h"
#include <algorithm>
#include <thread>

namespace Network {
namespace Database {

    ConnectionPool::ConnectionPool()
        : mInitialized(false)
        , mActiveConnections(0)
        , mMaxPoolSize(10)
        , mMinPoolSize(2)
        , mConnectionTimeout(std::chrono::seconds(30))
        , mIdleTimeout(std::chrono::seconds(300))
    {
    }

    ConnectionPool::~ConnectionPool() 
    {
        Shutdown();
    }

    bool ConnectionPool::Initialize(const DatabaseConfig& config) 
    {
        std::lock_guard<std::mutex> lock(mMutex);

        if (mInitialized.load()) 
        {
            return true;
        }

        mConfig = config;
        mMaxPoolSize = config.mMaxPoolSize;
        mMinPoolSize = config.mMinPoolSize;

        // English: Create database instance
        // 한글: 데이터베이스 인스턴스 생성
        mDatabase = DatabaseFactory::CreateDatabase(config.mType);
        if (!mDatabase) 
        {
            return false;
        }

        try 
        {
            mDatabase->Connect(config);

            // English: Pre-create minimum connections
            // 한글: 최소 연결 미리 생성
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
        catch (const DatabaseException&) 
        {
            return false;
        }
    }

    void ConnectionPool::Shutdown() 
    {
        if (!mInitialized.load()) 
        {
            return;
        }

        // English: Wait for all connections to be returned
        // 한글: 모든 연결이 반환될 때까지 대기
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCondition.wait_for(lock,
                                std::chrono::seconds(5),
                                [this] { return mActiveConnections.load() == 0; });
        }

        // English: Close all connections
        // 한글: 모든 연결 닫기
        std::lock_guard<std::mutex> lock(mMutex);
        Clear();

        // English: Disconnect database
        // 한글: 데이터베이스 연결 해제
        if (mDatabase) 
        {
            mDatabase->Disconnect();
            mDatabase.reset();
        }

        mInitialized.store(false);
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

        // English: Wait for available connection or timeout
        // 한글: 사용 가능한 연결 대기 또는 타임아웃
        bool acquired = mCondition.wait_for(lock, mConnectionTimeout, [this] 
        {
            // English: Check if any connection is available
            // 한글: 사용 가능한 연결이 있는지 확인
            for (auto& pooled : mConnections) 
            {
                if (!pooled.mInUse && pooled.mConnection->IsOpen()) 
                {
                    return true;
                }
            }

            // English: Can we create a new connection?
            // 한글: 새 연결을 생성할 수 있는가?
            return mConnections.size() < mMaxPoolSize;
        });

        if (!acquired) 
        {
            throw DatabaseException("Connection pool timeout - no connections available");
        }

        // English: Try to find an existing free connection
        // 한글: 기존 무료 연결 찾기 시도
        for (auto& pooled : mConnections) 
        {
            if (!pooled.mInUse && pooled.mConnection->IsOpen()) 
            {
                pooled.mInUse = true;
                pooled.mLastUsed = std::chrono::steady_clock::now();
                mActiveConnections.fetch_add(1);
                return pooled.mConnection;
            }
        }

        // English: Create a new connection if under limit
        // 한글: 제한 미만이면 새 연결 생성
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

        for (auto& pooled : mConnections) 
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

    void ConnectionPool::Clear() 
    {
        std::lock_guard<std::mutex> lock(mMutex);

        // English: Close all connections that are not in use
        // 한글: 사용 중이 아닌 모든 연결 닫기
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

    size_t ConnectionPool::GetActiveConnections() const 
    {
        return mActiveConnections.load();
    }

    size_t ConnectionPool::GetAvailableConnections() const 
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mMutex));
        size_t available = 0;
        for (const auto& pooled : mConnections) 
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
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mMutex));
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
                auto idleDuration = std::chrono::duration_cast<std::chrono::seconds>(
                    now - it->mLastUsed);

                if (idleDuration > mIdleTimeout && mConnections.size() > mMinPoolSize) 
                {
                    it->mConnection->Close();
                    it = mConnections.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

}  // namespace Database
}  // namespace Network
