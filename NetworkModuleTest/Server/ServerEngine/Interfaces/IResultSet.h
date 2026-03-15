#pragma once

// English: Abstract result set interface
// 한글: 추상 결과 집합 인터페이스

#include <string>

namespace Network
{
namespace Database
{

// =============================================================================
// English: IResultSet interface
// 한글: IResultSet 인터페이스
// =============================================================================

/**
 * English: Abstract result set interface
 * 한글: 추상 결과 집합 인터페이스
 */
class IResultSet
{
  public:
	virtual ~IResultSet() = default;

	// English: Navigation
	// 한글: 탐색
	virtual bool Next() = 0;

	// English: Null checking
	// 한글: Null 체크
	virtual bool IsNull(size_t columnIndex) = 0;
	virtual bool IsNull(const std::string &columnName) { return IsNull(FindColumn(columnName)); }

	// English: Data retrieval methods - by index
	// 한글: 데이터 조회 메서드 - 인덱스로
	virtual std::string GetString(size_t columnIndex) = 0;
	virtual int GetInt(size_t columnIndex) = 0;
	virtual long long GetLong(size_t columnIndex) = 0;
	virtual double GetDouble(size_t columnIndex) = 0;
	virtual bool GetBool(size_t columnIndex) = 0;

	// English: Data retrieval methods - by name (default: delegates to FindColumn + index variant)
	// 한글: 데이터 조회 메서드 - 이름으로 (기본: FindColumn + 인덱스 오버로드에 위임)
	virtual std::string GetString(const std::string &columnName) { return GetString(FindColumn(columnName)); }
	virtual int GetInt(const std::string &columnName)            { return GetInt(FindColumn(columnName)); }
	virtual long long GetLong(const std::string &columnName)     { return GetLong(FindColumn(columnName)); }
	virtual double GetDouble(const std::string &columnName)      { return GetDouble(FindColumn(columnName)); }
	virtual bool GetBool(const std::string &columnName)          { return GetBool(FindColumn(columnName)); }

	// English: Metadata
	// 한글: 메타데이터
	virtual size_t GetColumnCount() const = 0;
	virtual std::string GetColumnName(size_t columnIndex) const = 0;
	virtual size_t FindColumn(const std::string &columnName) const = 0;

	// English: Cleanup
	// 한글: 정리
	virtual void Close() = 0;
};

} // namespace Database
} // namespace Network
