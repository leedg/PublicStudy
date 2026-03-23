#pragma once

// 연결 풀(connection pool)의 추상 인터페이스.
// 구현체(ConnectionPool)는 mutex + condition_variable로 thread-safety를 보장하며,
// GetConnection()은 설정된 타임아웃까지 블록하다가 풀이 소진되면 nullptr를 반환한다.
// ScopedConnection을 사용하면 RAII 방식으로 자동 반환할 수 있다.

#include "IConnection.h"
#include <memory>

namespace Network
{
namespace Database
{

// =============================================================================
// IConnectionPool 인터페이스
// =============================================================================

class IConnectionPool
{
  public:
	virtual ~IConnectionPool() = default;

	// 풀에서 연결을 가져온다.
	// - 설정된 connectionTimeout까지 블록하며 유휴 연결이 생길 때까지 대기.
	// - 타임아웃 전에 연결이 확보되지 않으면 nullptr 반환 (풀 소진).
	// - 반환값을 반드시 확인하거나 ScopedConnection으로 감싸 IsValid()로 검증할 것.
	virtual std::shared_ptr<IConnection> GetConnection() = 0;

	// 연결을 풀에 반환한다.
	// ReturnConnection 호출 후 해당 shared_ptr은 더 이상 사용해서는 안 된다.
	virtual void ReturnConnection(std::shared_ptr<IConnection> pConnection) = 0;

	// 유휴(in-use가 아닌) 연결을 모두 닫고 풀에서 제거한다.
	virtual void Clear() = 0;

	// 현재 대여 중(in-use)인 연결 수
	virtual size_t GetActiveConnections() const = 0;

	// 현재 대여 가능한(유휴) 연결 수
	virtual size_t GetAvailableConnections() const = 0;
};

} // namespace Database
} // namespace Network
