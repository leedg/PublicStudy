#pragma once

// 고정 크기 char[] 버퍼 연산을 위한 안전한 크로스플랫폼 유틸리티.
//   패킷 구조체처럼 C 스타일 char[] 필드를 다룰 때 사용한다.
//   std::string 조작은 StringUtils.h를 사용한다.
//
// 반환값 규칙:
//   true  = 원본 전체가 들어감 (no truncation)
//   false = 잘렸거나 실패 (truncated or error)
//
// 호출 방식 (A) — 포인터 + 명시적 크기:
//   StringUtil::Copy(buf, sizeof(buf), src);
//
// 호출 방식 (B) — char[] 참조 (크기 자동 추론, 컴파일 타임):
//   StringUtil::Copy(packet.name, src);   // sizeof 불필요

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Network::Utils
{
// =============================================================================
// StringUtil — 고정 크기 char[] 버퍼의 안전한 연산.
//   항상 null 종료 보장. 오버플로우 없음. 크로스플랫폼.
// =============================================================================

class StringUtil
{
public:
	// =========================================================================
	// 헬퍼
	// =========================================================================

	// nullptr을 ""로 변환 — snprintf에 nullptr 전달 방지.
	static const char* NullToEmpty(const char* src)
	{
		return (src != nullptr) ? src : "";
	}

	// nullptr 안전 strlen — nullptr이면 0 반환.
	static size_t Length(const char* str)
	{
		return (str != nullptr) ? std::strlen(str) : 0;
	}

	// str이 nullptr이거나 빈 문자열이면 true.
	static bool IsEmpty(const char* str)
	{
		return (str == nullptr) || (str[0] == '\0');
	}

	// =========================================================================
	// Copy (A) — 포인터 + 명시적 크기
	// =========================================================================

	// src를 dest[0..destSize-1]에 복사. 항상 null 종료 보장.
	// 원본 전체가 들어가면 true, 잘렸거나 오류면 false.
	static bool Copy(char* dest, size_t destSize, const char* src)
	{
		if (dest == nullptr || destSize == 0)
			return false;

		const int written = std::snprintf(dest, destSize, "%s", NullToEmpty(src));

		if (written < 0)
		{
			dest[0] = '\0';
			return false;
		}

		return static_cast<size_t>(written) < destSize;
	}

	// =========================================================================
	// Copy (B) — char[] 참조 (크기 컴파일 타임 자동 추론)
	// 포인터에는 사용할 수 없음.
	// =========================================================================

	template<size_t N>
	static bool Copy(char (&dest)[N], const char* src)
	{
		return Copy(dest, N, src);
	}

	// =========================================================================
	// Format (A) — 포인터 + 명시적 크기 (가변 인자 템플릿, 타입 안전)
	// =========================================================================

	// printf 스타일 포맷을 dest에 출력. 항상 null 종료 보장.
	// va_args 대신 가변 인자 템플릿을 사용해 타입 안전성을 보장한다.
	// 전체 출력이 들어가면 true, 잘렸거나 오류면 false.
	template<typename... Args>
	static bool Format(char* dest, size_t destSize, const char* fmt, Args... args)
	{
		if (dest == nullptr || destSize == 0)
			return false;

		if (fmt == nullptr)
		{
			dest[0] = '\0';
			return false;
		}

		const int written = std::snprintf(dest, destSize, fmt, args...);

		if (written < 0)
		{
			dest[0] = '\0';
			return false;
		}

		return static_cast<size_t>(written) < destSize;
	}

	// =========================================================================
	// Format (B) — char[] 참조 (크기 컴파일 타임 자동 추론)
	// =========================================================================

	template<size_t N, typename... Args>
	static bool Format(char (&dest)[N], const char* fmt, Args... args)
	{
		return Format(dest, N, fmt, args...);
	}

	// =========================================================================
	// VFormat — 다른 가변 인자 래퍼 내부에서 사용하는 va_list 버전
	// =========================================================================

	static bool VFormat(char* dest, size_t destSize, const char* fmt, va_list args)
	{
		if (dest == nullptr || destSize == 0)
			return false;

		if (fmt == nullptr)
		{
			dest[0] = '\0';
			return false;
		}

		const int written = std::vsnprintf(dest, destSize, fmt, args);

		if (written < 0)
		{
			dest[0] = '\0';
			return false;
		}

		return static_cast<size_t>(written) < destSize;
	}

	// =========================================================================
	// Append (A) — 포인터 + 명시적 크기
	// =========================================================================

	// 기존 dest 내용 뒤에 src를 이어붙임. 항상 null 종료 보장.
	// 전체가 들어가면 true, 잘렸거나 오류면 false.
	static bool Append(char* dest, size_t destSize, const char* src)
	{
		if (dest == nullptr || destSize == 0)
			return false;

		const size_t currentLen = std::strlen(dest);
		if (currentLen >= destSize)
		{
			// 버퍼가 이미 오버런 상태 — 클램프 후 종료.
			dest[destSize - 1] = '\0';
			return false;
		}

		const size_t remain = destSize - currentLen;
		const int written = std::snprintf(dest + currentLen, remain, "%s", NullToEmpty(src));

		if (written < 0)
			return false;

		return static_cast<size_t>(written) < remain;
	}

	// =========================================================================
	// Append (B) — char[] 참조 (크기 컴파일 타임 자동 추론)
	// =========================================================================

	template<size_t N>
	static bool Append(char (&dest)[N], const char* src)
	{
		return Append(dest, N, src);
	}
};

} // namespace Network::Utils
