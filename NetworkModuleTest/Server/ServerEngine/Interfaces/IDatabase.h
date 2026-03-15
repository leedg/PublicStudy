#pragma once

// English: Abstract database interface
// 한글: 추상 데이터베이스 인터페이스

#include "DatabaseConfig.h"
#include "DatabaseType_enum.h"
#include <memory>

namespace Network
{
namespace Database
{

// English: Forward declarations
// 한글: 전방 선언
class IConnection;
class IStatement;

// =============================================================================
// English: IDatabase interface
// 한글: IDatabase 인터페이스
// =============================================================================

/**
 * English: Abstract database interface
 * 한글: 추상 데이터베이스 인터페이스
 */
class IDatabase
{
  public:
	virtual ~IDatabase() = default;

	// English: Connection management
	// 한글: 연결 관리
	virtual void Connect(const DatabaseConfig &config) = 0;
	virtual void Disconnect() = 0;
	virtual bool IsConnected() const = 0;

	// English: Object creation
	// 한글: 객체 생성
	virtual std::unique_ptr<IConnection> CreateConnection() = 0;
	virtual std::unique_ptr<IStatement> CreateStatement() = 0;

	// English: Database-level transaction management.
	//          NOTE: Transaction state is per-connection in most databases.
	//          Prefer using IConnection::BeginTransaction() on a connection obtained
	//          from CreateConnection() or ConnectionPool::GetConnection().
	//          Implementations may throw DatabaseException for this reason.
	// 한글: 데이터베이스 레벨 트랜잭션 관리.
	//       주의: 트랜잭션 상태는 대부분의 DB에서 연결 단위.
	//       CreateConnection() 또는 ConnectionPool::GetConnection()으로 얻은 연결에서
	//       IConnection::BeginTransaction()을 사용하는 것을 권장.
	//       구현체에 따라 DatabaseException이 발생할 수 있음.
	virtual void BeginTransaction() = 0;
	virtual void CommitTransaction() = 0;
	virtual void RollbackTransaction() = 0;

	// English: Information
	// 한글: 정보
	virtual DatabaseType GetType() const = 0;
	virtual const DatabaseConfig &GetConfig() const = 0;
};

} // namespace Database
} // namespace Network
