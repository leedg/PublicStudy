#pragma once

// English: Configuration management utility
// 한글: 설정 관리 유틸리티

#include "NetworkTypes.h"
#include <string>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <functional>
#include <algorithm>

namespace Network::Utils
{
// =============================================================================
// English: ConfigManager - manages server configuration
// 한글: ConfigManager - 서버 설정 관리
// =============================================================================

class ConfigManager
{
public:
	// English: Configuration structure
	// 한글: 설정 구조체
	struct Config
	{
		// English: Network settings
		// 한글: 네트워크 설정
		uint16_t port = DEFAULT_PORT;
		size_t maxConnections = MAX_CONNECTIONS;
		size_t bufferSize = DEFAULT_BUFFER_SIZE;
		int timeoutMs = DEFAULT_TIMEOUT_MS;

		// English: Logging settings
		// 한글: 로깅 설정
		std::string logLevel = "INFO";

		// English: Database settings
		// 한글: 데이터베이스 설정
		std::string databaseHost = "localhost";
		uint16_t databasePort = 5432;
		std::string databaseName = "networkdb";
		std::string databaseUser = "postgres";
		std::string databasePassword = "password";
	};

	// English: Helper function to trim whitespace efficiently
	// 한글: 공백을 효율적으로 제거하는 헬퍼 함수
	static std::string Trim(const std::string& str)
	{
		size_t start = str.find_first_not_of(" \t");
		if (start == std::string::npos)
			return "";
		size_t end = str.find_last_not_of(" \t");
		return str.substr(start, end - start + 1);
	}

	// English: Load configuration from file (optimized with map dispatch)
	// 한글: 파일에서 설정 로드 (맵 디스패치로 최적화)
	// @param filename - Path to configuration file
	// @return Loaded configuration (or default if file not found)
	static Config LoadFromFile(const std::string& filename)
	{
		Config config = GetDefault();
		std::ifstream file(filename);
		
		if (!file.is_open())
		{
			// English: File not found, return default config
			// 한글: 파일을 찾을 수 없음, 기본 설정 반환
			return config;
		}

		// English: Create handler map for O(1) dispatch
		// 한글: O(1) 디스패치를 위한 핸들러 맵 생성
		std::unordered_map<std::string, std::function<void(const std::string&)>> handlers;
		handlers["port"] = [&](const std::string& v) { config.port = static_cast<uint16_t>(std::stoi(v)); };
		handlers["maxConnections"] = [&](const std::string& v) { config.maxConnections = static_cast<size_t>(std::stoull(v)); };
		handlers["bufferSize"] = [&](const std::string& v) { config.bufferSize = static_cast<size_t>(std::stoull(v)); };
		handlers["timeoutMs"] = [&](const std::string& v) { config.timeoutMs = std::stoi(v); };
		handlers["logLevel"] = [&](const std::string& v) { config.logLevel = v; };
		handlers["databaseHost"] = [&](const std::string& v) { config.databaseHost = v; };
		handlers["databasePort"] = [&](const std::string& v) { config.databasePort = static_cast<uint16_t>(std::stoi(v)); };
		handlers["databaseName"] = [&](const std::string& v) { config.databaseName = v; };
		handlers["databaseUser"] = [&](const std::string& v) { config.databaseUser = v; };
		handlers["databasePassword"] = [&](const std::string& v) { config.databasePassword = v; };

		std::string line;
		while (std::getline(file, line))
		{
			// English: Skip empty lines and comments
			// 한글: 빈 줄과 주석 건너뛰기
			if (line.empty() || line[0] == '#' || line[0] == ';')
				continue;

			// English: Parse key=value pairs
			// 한글: key=value 쌍 파싱
			size_t pos = line.find('=');
			if (pos == std::string::npos)
				continue;

			std::string key = Trim(line.substr(0, pos));
			std::string value = Trim(line.substr(pos + 1));

			// English: Dispatch using map lookup (O(1))
			// 한글: 맵 조회를 사용한 디스패치 (O(1))
			auto it = handlers.find(key);
			if (it != handlers.end())
			{
				try
				{
					it->second(value);
				}
				catch (...)
				{
					// English: Ignore parse errors, keep default value
					// 한글: 파싱 오류 무시, 기본값 유지
				}
			}
		}

		file.close();
		return config;
	}

	// English: Save configuration to file (simple key=value format)
	// 한글: 파일에 설정 저장 (간단한 key=value 형식)
	// @param config - Configuration to save
	// @param filename - Path to configuration file
	// @return true if save succeeded, false otherwise
	static bool SaveToFile(const Config& config, const std::string& filename)
	{
		std::ofstream file(filename);
		
		if (!file.is_open())
		{
			return false;
		}

		// English: Write configuration header
		// 한글: 설정 헤더 작성
		file << "# Network Server Configuration\n";
		file << "# Generated configuration file\n\n";

		// English: Write network settings
		// 한글: 네트워크 설정 작성
		file << "# Network Settings\n";
		file << "port=" << config.port << "\n";
		file << "maxConnections=" << config.maxConnections << "\n";
		file << "bufferSize=" << config.bufferSize << "\n";
		file << "timeoutMs=" << config.timeoutMs << "\n\n";

		// English: Write logging settings
		// 한글: 로깅 설정 작성
		file << "# Logging Settings\n";
		file << "logLevel=" << config.logLevel << "\n\n";

		// English: Write database settings
		// 한글: 데이터베이스 설정 작성
		file << "# Database Settings\n";
		file << "databaseHost=" << config.databaseHost << "\n";
		file << "databasePort=" << config.databasePort << "\n";
		file << "databaseName=" << config.databaseName << "\n";
		file << "databaseUser=" << config.databaseUser << "\n";
		file << "databasePassword=" << config.databasePassword << "\n";

		file.close();
		return true;
	}

	// English: Get default configuration
	// 한글: 기본 설정 가져오기
	static Config GetDefault() { return Config{}; }

	// English: Validate configuration
	// 한글: 설정 유효성 검사
	// @param config - Configuration to validate
	// @return true if configuration is valid
	static bool ValidateConfig(const Config &config)
	{
		return config.port > 0 && config.maxConnections > 0;
	}
};

} // namespace Network::Utils
