#pragma once

// 단일 SQL 구문(prepared statement)의 추상 인터페이스.
// IDatabase가 "연결 수명 + 객체 생성"을 담당하는 것과 달리,
// IStatement는 "쿼리 텍스트 설정 → 파라미터 바인딩 → 실행"의 단계적 흐름을 담당.
// 하나의 IStatement 인스턴스는 단일 쿼리에 대응하며,
// 재사용 시 SetQuery()로 쿼리를 교체하고 ClearParameters()로 바인딩을 초기화한다.

#include <memory>
#include <string>
#include <vector>

namespace Network
{
namespace Database
{

// 전방 선언
class IResultSet;

// =============================================================================
// IStatement 인터페이스
// =============================================================================

class IStatement
{
  public:
	virtual ~IStatement() = default;

	// 쿼리 설정
	virtual void SetQuery(const std::string &query) = 0;
	// 구문 실행 제한 시간 (초). 0이면 타임아웃 없음.
	// SQLite처럼 statement 단위 timeout을 지원하지 않는 구현체는 no-op.
	virtual void SetTimeout(int seconds) = 0;

	// 파라미터 바인딩 — 인덱스는 1-based (첫 번째 파라미터 = 1).
	// 각 오버로드는 DB 드라이버의 네이티브 타입으로 직접 바인딩하여
	// 문자열 변환 경유 시 발생하는 정밀도 손실을 방지한다.
	virtual void BindParameter(size_t index, const std::string &value) = 0;
	virtual void BindParameter(size_t index, int value) = 0;
	virtual void BindParameter(size_t index, long long value) = 0;
	virtual void BindParameter(size_t index, double value) = 0;
	virtual void BindParameter(size_t index, bool value) = 0;
	virtual void BindNullParameter(size_t index) = 0;

	// 쿼리 실행
	// - ExecuteQuery(): SELECT 등 결과 집합 반환
	// - ExecuteUpdate(): INSERT/UPDATE/DELETE — 영향받은 행 수 반환
	// - Execute(): 결과 집합이 필요 없는 DDL/DML
	virtual std::unique_ptr<IResultSet> ExecuteQuery() = 0;
	virtual int ExecuteUpdate() = 0;
	virtual bool Execute() = 0;

	// 배치 작업 — 동일 쿼리를 여러 파라미터 셋으로 반복 실행할 때 사용.
	// AddBatch()로 현재 파라미터를 스냅샷 후 초기화, ExecuteBatch()로 일괄 실행.
	// 반환값은 각 배치 항목의 영향받은 행 수 (실패 항목은 -1).
	virtual void AddBatch() = 0;
	virtual std::vector<int> ExecuteBatch() = 0;

	// 파라미터 초기화 및 리소스 해제
	virtual void ClearParameters() = 0;
	virtual void Close() = 0;
};

} // namespace Database
} // namespace Network
