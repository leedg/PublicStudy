#pragma once

// 데이터베이스 백엔드(SQLite, ODBC, PostgreSQL 등)에 독립적인 추상 인터페이스.
// IDatabase는 "DB 연결 수명 및 객체 팩토리" 역할만 담당하며,
// 실제 쿼리 실행은 IStatement, 결과 소비는 IResultSet이 각각 담당한다.
// (IDatabase vs IStatement 책임 분리: 연결 관리와 쿼리 실행을 분리하여
//  ConnectionPool이 IDatabase 없이 IConnection만 대여할 수 있도록 설계)

#include "DatabaseConfig.h"
#include "DatabaseType_enum.h"
#include <memory>

namespace Network
{
namespace Database
{

// 전방 선언
class IConnection;
class IStatement;

// =============================================================================
// IDatabase 인터페이스
// =============================================================================

class IDatabase
{
  public:
	virtual ~IDatabase() = default;

	// 연결 관리
	virtual void Connect(const DatabaseConfig &config) = 0;
	virtual void Disconnect() = 0;
	virtual bool IsConnected() const = 0;

	// 객체 생성 팩토리
	// - CreateConnection(): 독립 수명의 연결 반환 (ConnectionPool에서 사용)
	// - CreateStatement(): 내부 전용 연결을 보유하는 단발성 statement 반환
	virtual std::unique_ptr<IConnection> CreateConnection() = 0;
	virtual std::unique_ptr<IStatement> CreateStatement() = 0;

	// 데이터베이스 레벨 트랜잭션 관리.
	// 주의: 트랜잭션 상태는 대부분의 DB에서 연결(connection) 단위.
	// CreateConnection() 또는 ConnectionPool::GetConnection()으로 얻은 연결에서
	// IConnection::BeginTransaction()을 사용하는 것을 권장.
	// 일부 구현체(ODBC 등)는 이 메서드에서 DatabaseException을 발생시킴.
	virtual void BeginTransaction() = 0;
	virtual void CommitTransaction() = 0;
	virtual void RollbackTransaction() = 0;

	// 메타 정보
	virtual DatabaseType GetType() const = 0;
	virtual const DatabaseConfig &GetConfig() const = 0;
};

} // namespace Database
} // namespace Network
