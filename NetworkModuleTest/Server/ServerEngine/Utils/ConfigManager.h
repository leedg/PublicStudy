#pragma once

// English: Configuration management utility
// 한글: 설정 관리 유틸리티

#include "NetworkTypes.h"
#include <string>
#include <cstdint>

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

	// English: Load configuration from file
	// 한글: 파일에서 설정 로드
	// @param filename - Path to configuration file
	// @return Loaded configuration (or default if file not found)
	static Config LoadFromFile(const std::string& /*filename*/)
	{
		// English: Stub - returns default config
		// 한글: 스텁 - 기본 설정 반환
		// TODO: Implement actual file loading
		return GetDefault();
	}

	// English: Save configuration to file
	// 한글: 파일에 설정 저장
	// @param config - Configuration to save
	// @param filename - Path to configuration file
	// @return true if save succeeded, false otherwise
	static bool SaveToFile(const Config& /*config*/, const std::string& /*filename*/)
	{
		// English: Stub
		// 한글: 스텁
		// TODO: Implement actual file saving
		return false;
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
