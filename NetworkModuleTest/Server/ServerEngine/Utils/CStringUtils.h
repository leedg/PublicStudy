#pragma once

// English: Safe, cross-platform utilities for fixed-size char buffer operations.
//          Use this when working with C-style char[] fields (e.g. packet structs).
//          For std::string operations, use StringUtils instead.
//
// 한글: 고정 크기 char 버퍼 연산을 위한 안전한 크로스플랫폼 유틸리티.
//       C 스타일 char[] 필드(예: 패킷 구조체)에서 사용.
//       std::string 연산은 StringUtils 사용.
//
// Return convention / 반환값 규칙:
//   true  = 원본 전체가 들어감 (no truncation)
//   false = 잘렸거나 실패 (truncated or error)
//
// Template overloads / 템플릿 오버로드:
//   char[] 배열을 직접 전달하면 크기가 자동으로 추론됩니다.
//   CStringUtils::Copy(packet.name, src);              // sizeof 불필요
//   CStringUtils::Format(packet.name, "%d", id);       // sizeof 불필요

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Network::Utils
{
// =============================================================================
// English: CStringUtils - safe operations on fixed-size char[] buffers.
//          Always null-terminates. Never overflows. Cross-platform.
//
// 한글: CStringUtils - 고정 크기 char[] 버퍼의 안전한 연산.
//       항상 null 종료 보장. 오버플로우 없음. 크로스플랫폼.
//
// Two calling styles / 두 가지 호출 방식:
//   (A) Pointer + explicit size  — works with any char* (pointer or array)
//   (B) char[] reference         — size auto-deduced, cannot be used on pointers
// =============================================================================

class CStringUtils
{
public:
	// =========================================================================
	// English: Helpers
	// 한글: 헬퍼
	// =========================================================================

	// English: Convert nullptr to "" so callers never pass nullptr to snprintf.
	// 한글: nullptr을 ""로 변환 — snprintf에 nullptr 전달 방지.
	static const char* NullToEmpty(const char* src)
	{
		return (src != nullptr) ? src : "";
	}

	// English: Null-safe strlen — returns 0 for nullptr.
	// 한글: nullptr 안전 strlen — nullptr이면 0 반환.
	static size_t Length(const char* str)
	{
		return (str != nullptr) ? std::strlen(str) : 0;
	}

	// English: Returns true if str is nullptr or an empty string.
	// 한글: str이 nullptr이거나 빈 문자열이면 true.
	static bool IsEmpty(const char* str)
	{
		return (str == nullptr) || (str[0] == '\0');
	}

	// =========================================================================
	// English: Copy  (A) pointer + size
	// 한글: 복사  (A) 포인터 + 크기
	// =========================================================================

	// English: Copy src into dest[0..destSize-1], always null-terminated.
	//          Returns true if src fit entirely, false if truncated or error.
	// 한글: src를 dest[0..destSize-1]에 복사. 항상 null 종료 보장.
	//       원본 전체가 들어가면 true, 잘렸거나 오류면 false.
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
	// English: Copy  (B) char[] reference — size auto-deduced at compile time
	// 한글: 복사  (B) char[] 참조 — 크기 컴파일 타임 자동 추론
	// =========================================================================

	// English: Same as Copy(char*, size_t, const char*) but size is deduced from
	//          the char[] array type. Cannot be called with a pointer.
	//   CStringUtils::Copy(packet.name, src);   // sizeof(packet.name) implicit
	// 한글: Copy(char*, size_t, const char*)와 동일하지만 char[] 배열 크기를 자동 추론.
	//       포인터에는 사용할 수 없음.
	template<size_t N>
	static bool Copy(char (&dest)[N], const char* src)
	{
		return Copy(dest, N, src);
	}

	// =========================================================================
	// English: Format  (A) pointer + size  (variadic template — type-safe)
	// 한글: 포맷  (A) 포인터 + 크기  (가변 인자 템플릿 — 타입 안전)
	// =========================================================================

	// English: printf-style format into dest. Always null-terminated.
	//          Uses variadic template instead of va_args for type safety.
	//          Returns true if output fit entirely, false if truncated or error.
	// 한글: printf 스타일 포맷을 dest에 출력. 항상 null 종료 보장.
	//       타입 안전을 위해 va_args 대신 가변 인자 템플릿 사용.
	//       전체 출력이 들어가면 true, 잘렸거나 오류면 false.
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
	// English: Format  (B) char[] reference — size auto-deduced at compile time
	// 한글: 포맷  (B) char[] 참조 — 크기 컴파일 타임 자동 추론
	// =========================================================================

	// English: Same as Format(char*, size_t, const char*, Args...) but size is
	//          deduced from the char[] array type.
	//   CStringUtils::Format(packet.name, "%s_%d", serverName, id);
	// 한글: Format(char*, size_t, ...)와 동일하지만 배열 크기를 자동 추론.
	template<size_t N, typename... Args>
	static bool Format(char (&dest)[N], const char* fmt, Args... args)
	{
		return Format(dest, N, fmt, args...);
	}

	// =========================================================================
	// English: VFormat — va_list version for use inside other variadic wrappers
	// 한글: VFormat — 다른 가변 인자 래퍼 함수 내부에서 사용하는 va_list 버전
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
	// English: Append  (A) pointer + size
	// 한글: 이어붙이기  (A) 포인터 + 크기
	// =========================================================================

	// English: Append src after existing content in dest. Always null-terminated.
	//          Returns true if src fit entirely, false if truncated or error.
	// 한글: 기존 dest 내용 뒤에 src를 이어붙임. 항상 null 종료 보장.
	//       전체가 들어가면 true, 잘렸거나 오류면 false.
	static bool Append(char* dest, size_t destSize, const char* src)
	{
		if (dest == nullptr || destSize == 0)
			return false;

		const size_t currentLen = std::strlen(dest);
		if (currentLen >= destSize)
		{
			// English: Buffer already overrun — clamp and bail.
			// 한글: 버퍼가 이미 오버런 상태 — 클램프 후 종료.
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
	// English: Append  (B) char[] reference — size auto-deduced at compile time
	// 한글: 이어붙이기  (B) char[] 참조 — 크기 컴파일 타임 자동 추론
	// =========================================================================

	template<size_t N>
	static bool Append(char (&dest)[N], const char* src)
	{
		return Append(dest, N, src);
	}
};

} // namespace Network::Utils
