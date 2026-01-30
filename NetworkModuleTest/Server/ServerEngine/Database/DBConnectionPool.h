#pragma once

// English: Database connection pool with RAII wrapper
// 한글: RAII 래퍼를 포함한 데이터베이스 연결 풀

#include "DBConnection.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <cstdint>

namespace Network::Database
{
    using DBConnectionRef = std::shared_ptr<DBConnection>;

    // =============================================================================
    // English: DBConnectionPool class
    // 한글: DBConnectionPool 클래스
    // =============================================================================

    class DBConnectionPool
    {
    public:
        static DBConnectionPool& Instance();

        // English: Initialize / Shutdown
        // 한글: 초기화 / 종료
        bool Initialize(const std::string& connectionString, uint32_t poolSize);
        void Shutdown();

        // English: Acquire / Release connection
        // 한글: 연결 획득 / 반환
        DBConnectionRef Acquire();
        void Release(DBConnectionRef connection);

        // English: Pool state
        // 한글: 풀 상태
        size_t GetAvailableCount() const;
        size_t GetTotalCount() const { return mTotalCount; }
        bool IsInitialized() const { return mInitialized; }

    private:
        DBConnectionPool() = default;
        ~DBConnectionPool() = default;

        DBConnectionPool(const DBConnectionPool&) = delete;
        DBConnectionPool& operator=(const DBConnectionPool&) = delete;

    private:
        std::queue<DBConnectionRef>     mConnections;
        mutable std::mutex              mMutex;
        std::condition_variable         mCondition;
        std::string                     mConnectionString;
        size_t                          mTotalCount = 0;
        bool                            mInitialized = false;
    };

    // =============================================================================
    // English: ScopedDBConnection - RAII wrapper for DB connection
    // 한글: ScopedDBConnection - DB 연결용 RAII 래퍼
    // =============================================================================

    class ScopedDBConnection
    {
    public:
        ScopedDBConnection()
            : mConnection(DBConnectionPool::Instance().Acquire())
        {
        }

        ~ScopedDBConnection()
        {
            if (mConnection)
            {
                DBConnectionPool::Instance().Release(mConnection);
            }
        }

        // English: Prevent copy, allow move
        // 한글: 복사 방지, 이동 허용
        ScopedDBConnection(const ScopedDBConnection&) = delete;
        ScopedDBConnection& operator=(const ScopedDBConnection&) = delete;

        DBConnection* operator->() { return mConnection.get(); }
        DBConnection& operator*() { return *mConnection; }
        bool IsValid() const { return mConnection != nullptr && mConnection->IsConnected(); }

    private:
        DBConnectionRef mConnection;
    };

} // namespace Network::Database
