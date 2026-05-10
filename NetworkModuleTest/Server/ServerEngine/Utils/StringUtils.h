#pragma once

// std::string 조작 유틸리티.
// 고정 크기 char[] 버퍼 연산은 StringUtil.h를 사용한다.

#include <algorithm>
#include <string>
#include <vector>
#include <sstream>

namespace Network::Utils
{
// =============================================================================
// StringUtils — std::string 기반 문자열 조작 유틸리티
// =============================================================================

class StringUtils
{
public:
	// 문자열 양쪽 끝의 공백(스페이스, 탭, 개행) 제거
	static std::string Trim(const std::string &str)
	{
		size_t start = str.find_first_not_of(" \t\n\r");
		if (start == std::string::npos)
			return "";

		size_t end = str.find_last_not_of(" \t\n\r");
		return str.substr(start, end - start + 1);
	}

	// 지정 구분자로 문자열 분리
	static std::vector<std::string> Split(const std::string &str, char delimiter)
	{
		std::vector<std::string> result;
		std::stringstream ss(str);
		std::string item;

		while (std::getline(ss, item, delimiter))
		{
			result.push_back(item);
		}

		return result;
	}

	// 문자열이 비어있거나 공백만 포함하면 true
	static bool IsEmpty(const std::string &str)
	{
		return str.empty() || Trim(str).empty();
	}

	// 문자열을 대문자로 변환
	static std::string ToUpper(const std::string &str)
	{
		std::string result = str;
		std::transform(result.begin(), result.end(), result.begin(), ::toupper);
		return result;
	}

	// 문자열을 소문자로 변환
	static std::string ToLower(const std::string &str)
	{
		std::string result = str;
		std::transform(result.begin(), result.end(), result.begin(), ::tolower);
		return result;
	}
};

} // namespace Network::Utils
