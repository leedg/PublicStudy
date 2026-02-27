#pragma once

// English: Abstract statement interface
// 한글: 추상 구문 인터페이스

#include <memory>
#include <string>
#include <vector>

namespace Network
{
namespace Database
{

// English: Forward declaration
// 한글: 전방 선언
class IResultSet;

// =============================================================================
// English: IStatement interface
// 한글: IStatement 인터페이스
// =============================================================================

/**
 * English: Abstract statement interface
 * 한글: 추상 구문 인터페이스
 */
class IStatement
{
  public:
	virtual ~IStatement() = default;

	// English: Query configuration
	// 한글: 쿼리 설정
	virtual void SetQuery(const std::string &query) = 0;
	virtual void SetTimeout(int seconds) = 0;

	// English: Parameter binding
	// 한글: 파라미터 바인딩
	virtual void BindParameter(size_t index, const std::string &value) = 0;
	virtual void BindParameter(size_t index, int value) = 0;
	virtual void BindParameter(size_t index, long long value) = 0;
	virtual void BindParameter(size_t index, double value) = 0;
	virtual void BindParameter(size_t index, bool value) = 0;
	virtual void BindNullParameter(size_t index) = 0;

	// English: Query execution
	// 한글: 쿼리 실행
	virtual std::unique_ptr<IResultSet> ExecuteQuery() = 0;
	virtual int ExecuteUpdate() = 0;
	virtual bool Execute() = 0;

	// English: Batch operations
	// 한글: 배치 작업
	virtual void AddBatch() = 0;
	virtual std::vector<int> ExecuteBatch() = 0;

	// English: Cleanup
	// 한글: 정리
	virtual void ClearParameters() = 0;
	virtual void Close() = 0;
};

} // namespace Database
} // namespace Network
