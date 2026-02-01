#pragma once

// English: Database connection pool with RAII wrapper
// ?쒓?: RAII ?섑띁瑜??ы븿???곗씠?곕쿋?댁뒪 ?곌껐 ?

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
    // ?쒓?: DBConnectionPool ?대옒??
    // =============================================================================

    class DBConnectionPool
    {
    public:
        static DBConnectionPool& Instance();

        // English: Initialize / Shutdown
        // ?쒓?: 珥덇린??/ 醫낅즺
        bool Initialize(const std::string& connectionString, uint32_t poolSize);
        void Shutdown();

        // English: Acquire / Release connection
        // ?쒓?: ?곌껐 ?띾뱷 / 諛섑솚
        DBConnectionRef Acquire();
        void Release(DBConnectionRef connection);

        // English: Pool state
        // ?쒓?: ? ?곹깭
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
    // ?쒓?: ScopedDBConnection - DB ?곌껐??RAII ?섑띁
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
        // ?쒓?: 蹂듭궗 諛⑹?, ?대룞 ?덉슜
        ScopedDBConnection(const ScopedDBConnection&) = delete;
        ScopedDBConnection& operator=(const ScopedDBConnection&) = delete;

        DBConnection* operator->() { return mConnection.get(); }
        DBConnection& operator*() { return *mConnection; }
        bool IsValid() const { return mConnection != nullptr && mConnection->IsConnected(); }

    private:
        DBConnectionRef mConnection;
    };

} // namespace Network::Database

