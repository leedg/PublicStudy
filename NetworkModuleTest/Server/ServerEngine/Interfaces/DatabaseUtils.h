#pragma once

// 데이터베이스 관련 유틸리티 함수 모음.
// 연결 문자열 빌더와 타입 안전 파라미터 바인딩 헬퍼를 제공한다.

#include "IStatement.h"
#include <map>
#include <string>

namespace Network
{
namespace Database
{

// =============================================================================
// 데이터베이스 유틸리티 함수
// =============================================================================

namespace Utils
{

// ODBC 연결 문자열 빌드.
// params 맵의 키-값 쌍을 "Key=Value;Key=Value" 형식으로 이어 붙인다.
// 예: {{"DSN","MyDB"},{"UID","user"},{"PWD","pass"}} → "DSN=MyDB;UID=user;PWD=pass"
std::string
BuildODBCConnectionString(const std::map<std::string, std::string> &params);

// OLE DB 연결 문자열 빌드.
// ODBC와 동일한 "Key=Value;" 형식이지만, 키 이름은 OLE DB 공급자 문서 참고.
// 예: {{"Provider","SQLOLEDB"},{"Data Source","server"},{"Initial Catalog","db"}}
std::string
BuildOLEDBConnectionString(const std::map<std::string, std::string> &params);

// 타입 안전 파라미터 바인딩 헬퍼 (제네릭 선언 — 특수화로만 사용).
// BindParameter 오버로드를 직접 호출하는 것과 기능은 동일하나,
// 템플릿 인자로 타입을 명시하여 컴파일 타임에 미지원 타입을 차단할 수 있다.
template <typename T>
void BindParameterSafe(IStatement *pStmt, size_t index, const T &value);

// std::string 특수화
template <>
void BindParameterSafe<std::string>(IStatement *pStmt, size_t index,
									const std::string &value);

// int 특수화
template <>
void BindParameterSafe<int>(IStatement *pStmt, size_t index, const int &value);

// long long 특수화
template <>
void BindParameterSafe<long long>(IStatement *pStmt, size_t index,
								  const long long &value);

// double 특수화
template <>
void BindParameterSafe<double>(IStatement *pStmt, size_t index,
								   const double &value);

// bool 특수화
template <>
void BindParameterSafe<bool>(IStatement *pStmt, size_t index,
							 const bool &value);

} // namespace Utils

} // namespace Database
} // namespace Network
