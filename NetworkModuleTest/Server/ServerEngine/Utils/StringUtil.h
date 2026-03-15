#pragma once

// Safe, cross-platform utilities for fixed-size char buffer operations.
//          Use this when working with C-style char[] fields (e.g. packet structs).
//          For std::string operations, use StringUtils instead.
//
//
//

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Network::Utils
{
// =============================================================================
// StringUtil - safe operations on fixed-size char[] buffers.
//          Always null-terminates. Never overflows. Cross-platform.
//
//
//   (A) Pointer + explicit size  — works with any char* (pointer or array)
//   (B) char[] reference         — size auto-deduced, cannot be used on pointers
// =============================================================================

class StringUtil
{
public:
	// =========================================================================
	// Helpers
	// =========================================================================

	// Convert nullptr to "" so callers never pass nullptr to snprintf.
	static const char* NullToEmpty(const char* src)
	{
		return (src != nullptr) ? src : "";
	}

	// Null-safe strlen — returns 0 for nullptr.
	static size_t Length(const char* str)
	{
		return (str != nullptr) ? std::strlen(str) : 0;
	}

	// Returns true if str is nullptr or an empty string.
	static bool IsEmpty(const char* str)
	{
		return (str == nullptr) || (str[0] == '\0');
	}

	// =========================================================================
	// Copy  (A) pointer + size
	// =========================================================================

	// Copy src into dest[0..destSize-1], always null-terminated.
	//          Returns true if src fit entirely, false if truncated or error.
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
	// Copy  (B) char[] reference — size auto-deduced at compile time
	// =========================================================================

	// Same as Copy(char*, size_t, const char*) but size is deduced from
	//          the char[] array type. Cannot be called with a pointer.
	//   StringUtil::Copy(packet.name, src);   // sizeof(packet.name) implicit
	template<size_t N>
	static bool Copy(char (&dest)[N], const char* src)
	{
		return Copy(dest, N, src);
	}

	// =========================================================================
	// Format  (A) pointer + size  (variadic template — type-safe)
	// =========================================================================

	// printf-style format into dest. Always null-terminated.
	//          Uses variadic template instead of va_args for type safety.
	//          Returns true if output fit entirely, false if truncated or error.
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
	// Format  (B) char[] reference — size auto-deduced at compile time
	// =========================================================================

	// Same as Format(char*, size_t, const char*, Args...) but size is
	//          deduced from the char[] array type.
	//   StringUtil::Format(packet.name, "%s_%d", serverName, id);
	template<size_t N, typename... Args>
	static bool Format(char (&dest)[N], const char* fmt, Args... args)
	{
		return Format(dest, N, fmt, args...);
	}

	// =========================================================================
	// VFormat — va_list version for use inside other variadic wrappers
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
	// Append  (A) pointer + size
	// =========================================================================

	// Append src after existing content in dest. Always null-terminated.
	//          Returns true if src fit entirely, false if truncated or error.
	static bool Append(char* dest, size_t destSize, const char* src)
	{
		if (dest == nullptr || destSize == 0)
			return false;

		const size_t currentLen = std::strlen(dest);
		if (currentLen >= destSize)
		{
			// Buffer already overrun — clamp and bail.
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
	// Append  (B) char[] reference — size auto-deduced at compile time
	// =========================================================================

	template<size_t N>
	static bool Append(char (&dest)[N], const char* src)
	{
		return Append(dest, N, src);
	}
};

} // namespace Network::Utils
