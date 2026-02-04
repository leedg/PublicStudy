#pragma once

// English: Configuration management utility
// 한글: 설정 관리 유틸리티

#include "NetworkTypes.h"
#include <string>
#include <cstdint>
#include <fstream>
#include <sstream>

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

	// English: Load configuration from file (simple key=value format)
	// 한글: 파일에서 설정 로드 (간단한 key=value 형식)
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

			std::string key = line.substr(0, pos);
			std::string value = line.substr(pos + 1);

			// English: Trim whitespace
			// 한글: 공백 제거
			key.erase(0, key.find_first_not_of(" \t"));
			key.erase(key.find_last_not_of(" \t") + 1);
			value.erase(0, value.find_first_not_of(" \t"));
			value.erase(value.find_last_not_of(" \t") + 1);

			// English: Set configuration values
			// 한글: 설정 값 설정
			if (key == "port")
				config.port = static_cast<uint16_t>(std::stoi(value));
			else if (key == "maxConnections")
				config.maxConnections = static_cast<size_t>(std::stoull(value));
			else if (key == "bufferSize")
				config.bufferSize = static_cast<size_t>(std::stoull(value));
			else if (key == "timeoutMs")
				config.timeoutMs = std::stoi(value);
			else if (key == "logLevel")
				config.logLevel = value;
			else if (key == "databaseHost")
				config.databaseHost = value;
			else if (key == "databasePort")
				config.databasePort = static_cast<uint16_t>(std::stoi(value));
			else if (key == "databaseName")
				config.databaseName = value;
			else if (key == "databaseUser")
				config.databaseUser = value;
			else if (key == "databasePassword")
				config.databasePassword = value;
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
