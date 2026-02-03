#pragma once

// English: Database configuration structure
// 한글: 데이터베이스 설정 구조체

#include "DatabaseType_enum.h"
#include <string>

namespace Network
{
namespace Database
{

// =============================================================================
// English: DatabaseConfig structure
// 한글: DatabaseConfig 구조체
// =============================================================================

/**
 * English: Database configuration structure
 * 한글: 데이터베이스 설정 구조체
 */
struct DatabaseConfig
{
	// English: Connection string
	// 한글: 연결 문자열
	std::string mConnectionString;

	// English: Database type
	// 한글: 데이터베이스 타입
	DatabaseType mType = DatabaseType::ODBC;

	// English: Connection timeout in seconds
	// 한글: 연결 타임아웃 (초)
	int mConnectionTimeout = 30;

	// English: Command timeout in seconds
	// 한글: 명령 타임아웃 (초)
	int mCommandTimeout = 30;

	// English: Auto-commit mode
	// 한글: 자동 커밋 모드
	bool mAutoCommit = true;

	// English: Maximum pool size
	// 한글: 최대 풀 크기
	int mMaxPoolSize = 10;

	// English: Minimum pool size
	// 한글: 최소 풀 크기
	int mMinPoolSize = 2;
};

} // namespace Database
} // namespace Network
