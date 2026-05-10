#pragma once

// SQL 쿼리 결과 집합의 추상 인터페이스.
// 커서 기반 순방향 탐색(forward-only)을 기본으로 설계되었으며,
// Next()를 호출한 후에야 Get*() / IsNull()을 호출할 수 있다.

#include <string>

namespace Network
{
namespace Database
{

// =============================================================================
// IResultSet 인터페이스
// =============================================================================

class IResultSet
{
  public:
	virtual ~IResultSet() = default;

	// 다음 행으로 이동. 행이 있으면 true, 없으면 false 반환.
	// Get*() 호출 전에 반드시 Next()가 true를 반환해야 한다.
	virtual bool Next() = 0;

	// NULL 여부 확인 (컬럼 인덱스 기반 — 0-based)
	virtual bool IsNull(size_t columnIndex) = 0;
	// NULL 여부 확인 (컬럼명 기반 — FindColumn 위임)
	virtual bool IsNull(const std::string &columnName) { return IsNull(FindColumn(columnName)); }

	// 데이터 조회 — 인덱스 기반 (0-based)
	virtual std::string GetString(size_t columnIndex) = 0;
	virtual int GetInt(size_t columnIndex) = 0;
	virtual long long GetLong(size_t columnIndex) = 0;
	virtual double GetDouble(size_t columnIndex) = 0;
	virtual bool GetBool(size_t columnIndex) = 0;

	// 데이터 조회 — 컬럼명 기반 (기본 구현: FindColumn + 인덱스 오버로드에 위임)
	// 성능이 중요한 경우 인덱스 오버로드를 직접 사용하거나
	// 구현체에서 이 메서드를 오버라이드하여 최적화할 수 있다.
	virtual std::string GetString(const std::string &columnName) { return GetString(FindColumn(columnName)); }
	virtual int GetInt(const std::string &columnName)            { return GetInt(FindColumn(columnName)); }
	virtual long long GetLong(const std::string &columnName)     { return GetLong(FindColumn(columnName)); }
	virtual double GetDouble(const std::string &columnName)      { return GetDouble(FindColumn(columnName)); }
	virtual bool GetBool(const std::string &columnName)          { return GetBool(FindColumn(columnName)); }

	// 메타데이터
	virtual size_t GetColumnCount() const = 0;
	virtual std::string GetColumnName(size_t columnIndex) const = 0;
	// 컬럼명으로 0-based 인덱스 반환. 없으면 DatabaseException.
	// 구현체는 대소문자 무시(case-insensitive) 비교를 권장 —
	// 드라이버마다 컬럼명 대소문자가 다를 수 있기 때문.
	virtual size_t FindColumn(const std::string &columnName) const = 0;

	// 리소스 해제
	virtual void Close() = 0;
};

} // namespace Database
} // namespace Network
