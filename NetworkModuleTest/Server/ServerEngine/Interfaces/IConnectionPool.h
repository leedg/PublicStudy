#pragma once

// English: Connection pool interface
// 한글: 연결 풀 인터페이스

#include "IConnection.h"
#include <memory>

namespace Network
{
namespace Database
{

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

	// English: Get a connection from the pool. Blocks up to the configured connection
	//          timeout. Returns nullptr if the timeout expires before a connection
	//          becomes available (pool exhausted). Always check the return value before use,
	//          or wrap in ScopedConnection and call IsValid().
	// 한글: 풀에서 연결을 가져옴. 설정된 연결 타임아웃까지 블록.
	//       타임아웃 전에 연결이 확보되지 않으면 nullptr 반환 (풀 소진).
	//       반환값을 반드시 확인하거나 ScopedConnection으로 감싸 IsValid()로 검증할 것.
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

} // namespace Database
} // namespace Network
