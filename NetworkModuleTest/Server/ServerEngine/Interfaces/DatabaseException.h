#pragma once

// 데이터베이스 레이어 전용 예외 클래스.
// std::exception을 상속하므로 범용 catch (const std::exception &)으로도 잡힌다.
// mErrorCode에는 드라이버 고유 에러 코드(ODBC native error, HRESULT, SQLite rc 등)가
// 저장되며, 0이면 코드 없음을 의미한다.

#include <exception>
#include <string>

namespace Network
{
namespace Database
{

// =============================================================================
// DatabaseException 클래스
// =============================================================================

class DatabaseException : public std::exception
{
  public:
	// message: 사람이 읽을 수 있는 에러 설명
	// errorCode: 드라이버 고유 에러 코드 (없으면 0)
	DatabaseException(const std::string &message, int errorCode = 0)
		: mMessage(message), mErrorCode(errorCode)
	{
	}

	// std::exception::what() 구현 — 에러 메시지 반환
	const char *what() const noexcept override { return mMessage.c_str(); }

	// 드라이버 고유 에러 코드 반환 (0이면 코드 없음)
	int GetErrorCode() const { return mErrorCode; }

  private:
	std::string mMessage;   // what()가 반환하는 사람이 읽을 수 있는 에러 설명
	int mErrorCode;         // 드라이버 고유 에러 코드 (0이면 코드 없음)
};

} // namespace Database
} // namespace Network
