#pragma once

// English: Connection pool interface
// 한글: 연결 풀 인터페이스

#include "IConnection.h"
#include <memory>

namespace Network {
namespace Database {

    // =============================================================================
    // English: IConnectionPool interface
    // 한글: IConnectionPool 인터페이스
    // =============================================================================

    /**
     * English: Connection pool interface
     * 한글: 연결 풀 인터페이스
     */
    class IConnectionPool 
    {
    public:
        virtual ~IConnectionPool() = default;

        // English: Get a connection from the pool
        // 한글: 풀에서 연결 가져오기
        virtual std::shared_ptr<IConnection> GetConnection() = 0;

        // English: Return a connection to the pool
        // 한글: 풀에 연결 반환하기
        virtual void ReturnConnection(std::shared_ptr<IConnection> pConnection) = 0;

        // English: Clear all connections
        // 한글: 모든 연결 지우기
        virtual void Clear() = 0;

        // English: Get number of active connections
        // 한글: 활성 연결 수 조회
        virtual size_t GetActiveConnections() const = 0;

        // English: Get number of available connections
        // 한글: 사용 가능한 연결 수 조회
        virtual size_t GetAvailableConnections() const = 0;
    };

}  // namespace Database
}  // namespace Network
