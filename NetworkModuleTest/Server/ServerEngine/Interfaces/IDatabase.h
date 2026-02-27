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

	// English: Transaction management
	// 한글: 트랜잭션 관리
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
