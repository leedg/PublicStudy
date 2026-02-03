#pragma once

// English: Database utility functions
// 한글: 데이터베이스 유틸리티 함수

#include "IStatement.h"
#include <map>
#include <string>

namespace Network
{
namespace Database
{

// =============================================================================
// English: Database utility functions
// 한글: 데이터베이스 유틸리티 함수
// =============================================================================

/**
 * English: Utility functions
 * 한글: 유틸리티 함수
 */
namespace Utils
{
// English: Build connection strings
// 한글: 연결 문자열 빌드
std::string
BuildODBCConnectionString(const std::map<std::string, std::string> &params);
std::string
BuildOLEDBConnectionString(const std::map<std::string, std::string> &params);

// English: Type-safe parameter binding helpers
// 한글: 타입 안전 파라미터 바인딩 헬퍼
template <typename T>
void BindParameterSafe(IStatement *pStmt, size_t index, const T &value);

// English: Specializations
// 한글: 특수화
template <>
void BindParameterSafe<std::string>(IStatement *pStmt, size_t index,
									const std::string &value);

template <>
void BindParameterSafe<int>(IStatement *pStmt, size_t index, const int &value);

template <>
void BindParameterSafe<long long>(IStatement *pStmt, size_t index,
								  const long long &value);

template <>
void BindParameterSafe<double>(IStatement *pStmt, size_t index,
								   const double &value);

template <>
void BindParameterSafe<bool>(IStatement *pStmt, size_t index,
							 const bool &value);
} // namespace Utils

} // namespace Database
} // namespace Network
