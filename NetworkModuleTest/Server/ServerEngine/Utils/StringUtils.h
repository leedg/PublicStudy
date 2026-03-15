#pragma once

// String utility functions

#include <algorithm>
#include <string>
#include <vector>
#include <sstream>

namespace Network::Utils
{
// =============================================================================
// StringUtils - string manipulation utilities
// =============================================================================

class StringUtils
{
public:
	// Trim whitespace from both ends of a string
	static std::string Trim(const std::string &str)
	{
		size_t start = str.find_first_not_of(" \t\n\r");
		if (start == std::string::npos)
			return "";

		size_t end = str.find_last_not_of(" \t\n\r");
		return str.substr(start, end - start + 1);
	}

	// Split string by delimiter
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

	// Check if string is empty or contains only whitespace
	static bool IsEmpty(const std::string &str)
	{
		return str.empty() || Trim(str).empty();
	}

	// Convert string to uppercase
	static std::string ToUpper(const std::string &str)
	{
		std::string result = str;
		std::transform(result.begin(), result.end(), result.begin(), ::toupper);
		return result;
	}

	// Convert string to lowercase
	static std::string ToLower(const std::string &str)
	{
		std::string result = str;
		std::transform(result.begin(), result.end(), result.begin(), ::tolower);
		return result;
	}
};

} // namespace Network::Utils
