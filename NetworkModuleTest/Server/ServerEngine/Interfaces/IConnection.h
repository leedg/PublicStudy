#pragma once

// 단일 DB 연결(connection)의 추상 인터페이스.
// IDatabase가 연결 팩토리라면, IConnection은 실제 물리 연결을 래핑하며
// 트랜잭션 범위를 연결 단위로 관리하는 책임을 갖는다.
// ConnectionPool에서 대여·반환되는 단위이기도 하다.

#include <memory>
#include <string>

namespace Network
{
namespace Database
{

// 전방 선언
class IStatement;

// =============================================================================
// IConnection 인터페이스
// =============================================================================

class IConnection
{
  public:
	virtual ~IConnection() = default;

	// 연결 관리
	virtual void Open(const std::string &connectionString) = 0;
	virtual void Close() = 0;
	virtual bool IsOpen() const = 0;

	// 이 연결에 대해 구문(statement)을 생성.
	// 반환된 IStatement의 생존 기간 동안 이 연결이 유효해야 한다.
	virtual std::unique_ptr<IStatement> CreateStatement() = 0;

	// 트랜잭션 관리 — 연결 단위로 동작.
	// BeginTransaction()은 자동 커밋(auto-commit)을 비활성화하며,
	// Commit/Rollback 후 자동 커밋 상태로 복구하는 것이 구현 관례.
	virtual void BeginTransaction() = 0;
	virtual void CommitTransaction() = 0;
	virtual void RollbackTransaction() = 0;

	// 마지막 오류 정보 조회 (오류 없으면 코드=0, 메시지 빈 문자열)
	virtual int GetLastErrorCode() const = 0;
	virtual std::string GetLastError() const = 0;
};

} // namespace Database
} // namespace Network
