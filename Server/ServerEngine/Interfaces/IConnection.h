#pragma once

// English: Abstract connection interface
// 한글: 추상 연결 인터페이스

#include <memory>
#include <string>

namespace Network
{
namespace Database
{

// English: Forward declaration
// 한글: 전방 선언
class IStatement;

// =============================================================================
// English: IConnection interface
// 한글: IConnection 인터페이스
// =============================================================================

/**
 * English: Abstract connection interface
 * 한글: 추상 연결 인터페이스
 */
class IConnection
{
  public:
	virtual ~IConnection() = default;

	// English: Connection management
	// 한글: 연결 관리
	virtual void Open(const std::string &connectionString) = 0;
	virtual void Close() = 0;
	virtual bool IsOpen() const = 0;

	// English: Statement creation
	// 한글: 구문 생성
	virtual std::unique_ptr<IStatement> CreateStatement() = 0;

	// English: Transaction management
	// 한글: 트랜잭션 관리
	virtual void BeginTransaction() = 0;
	virtual void CommitTransaction() = 0;
	virtual void RollbackTransaction() = 0;

	// English: Error information
	// 한글: 에러 정보
	virtual int GetLastErrorCode() const = 0;
	virtual std::string GetLastError() const = 0;
};

} // namespace Database
} // namespace Network
