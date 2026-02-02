#pragma once

// English: Database connection pool with RAII wrapper
// ???: RAII ??묐쓠????釉???怨쀬뵠?怨뺤퓢??곷뮞 ?怨뚭퍙 ??

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
    // ???: DBConnectionPool ?????
    // =============================================================================

    class DBConnectionPool
    {
    public:
        static DBConnectionPool& Instance();

        // English: Initialize / Shutdown
        // ???: ?λ뜃由??/ ?ル굝利?
        bool Initialize(const std::string& connectionString, uint32_t poolSize);
        void Shutdown();

        // English: Acquire / Release connection
        // ???: ?怨뚭퍙 ??얜굣 / 獄쏆꼹??
        DBConnectionRef Acquire();
        void Release(DBConnectionRef connection);

        // English: Pool state
        // ???: ?? ?怨밴묶
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
    // ???: ScopedDBConnection - DB ?怨뚭퍙??RAII ??묐쓠
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
        // ???: 癰귣벊沅?獄쎻뫗?, ??猷???됱뒠
        ScopedDBConnection(const ScopedDBConnection&) = delete;
        ScopedDBConnection& operator=(const ScopedDBConnection&) = delete;

        DBConnection* operator->() { return mConnection.get(); }
        DBConnection& operator*() { return *mConnection; }
        bool IsValid() const { return mConnection != nullptr && mConnection->IsConnected(); }

    private:
        DBConnectionRef mConnection;
    };

} // namespace Network::Database

