// English: Configuration manager for NetworkModuleTest
// Korean: NetworkModuleTest 설정 관리자

#pragma once

#include <string>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Network::Utils
{

// =============================================================================
// English: Network configuration values
// Korean: 네트워크 설정값
// =============================================================================

struct NetworkConfig
{
	uint16_t ListenPort = 19010;
	uint16_t DBServerPort = 18002;
	std::string DBServerHost = "127.0.0.1";
	std::string EngineType = "auto";

	size_t MaxConnections = 1000;
	size_t SessionPoolCapacity = 1000;
	size_t SendBufferSize = 65536;
	size_t RecvBufferSize = 65536;
	size_t MaxLogicQueueDepth = 10000;

	uint32_t WorkerThreadCount = 0; // 0 = auto (hardware_concurrency)
	uint32_t AcceptThreadCount = 1;

	bool EnableNagle = false;
	bool EnableKeepAlive = true;
	uint32_t KeepAliveIdleSec = 60;
	uint32_t KeepAliveIntervalSec = 10;
	uint32_t KeepAliveCount = 3;
};

// =============================================================================
// English: Timeout configuration values
// Korean: 타임아웃 설정값
// =============================================================================

struct TimeoutConfig
{
	uint32_t ConnectTimeoutMs = 5000;
	uint32_t RecvTimeoutMs = 30000;
	uint32_t SendTimeoutMs = 10000;
	uint32_t PingIntervalMs = 5000;
	uint32_t PingTimeoutMs = 10000;
	uint32_t ReconnectIntervalMs = 1000;
	uint32_t MaxReconnectIntervalMs = 30000;
	uint32_t GracefulShutdownTimeoutMs = 8000;

	// English: Docker containers use shorter timeouts
	// 한글: Docker 컨테이너는 짧은 타임아웃 사용
#ifdef DOCKER_CONTAINER
	static constexpr bool IsDocker = true;
#else
	static constexpr bool IsDocker = false;
#endif
};

// =============================================================================
// English: Logging configuration values
// Korean: 로깅 설정값
// =============================================================================

struct LogConfig
{
	std::string Level = "INFO";
	std::string LogDir = "./logs";
	std::string LogFilePrefix = "netmod";
	bool EnableConsole = true;
	bool EnableFile = false;
	bool EnableTimestamp = true;
	size_t MaxFileSizeMB = 50;
	size_t MaxFileCount = 5;
};

// =============================================================================
// English: Database configuration values
// Korean: 데이터베이스 설정값
// =============================================================================

struct DBConfig
{
	std::string ConnectionString;
	std::string Driver = "SQLite";
	std::string Host = "127.0.0.1";
	uint16_t Port = 18002;
	uint32_t PoolSize = 5;
	uint32_t QueryTimeoutMs = 5000;
	uint32_t ReconnectIntervalMs = 3000;
	bool EnableRetry = true;
	size_t MaxRetryCount = 3;
};

// =============================================================================
// English: ConfigManager — central configuration registry
// Korean: ConfigManager — 중앙 설정 레지스트리
// =============================================================================

class ConfigManager
{
public:
	static ConfigManager& Instance();

	// English: Initialize with defaults or load from environment/file
	// 한글: 기본값으로 초기화 또는 환경/파일에서 로드
	void Initialize();

	// English: Load from environment variables (overrides defaults)
	// 한글: 환경 변수에서 로드 (기본값 덮어쓰기)
	void LoadFromEnv();

	// English: Get configuration sections (const reference)
	// 한글: 설정 섹션 가져오기 (const 참조)
	const NetworkConfig& GetNetwork() const { return mNetwork; }
	const TimeoutConfig& GetTimeout() const { return mTimeout; }
	const LogConfig& GetLog() const { return mLog; }
	const DBConfig& GetDB() const { return mDB; }

	// English: Mutable access (use sparingly, mainly during startup)
	// 한글: 수정 가능 접근 (주로 시작 단계에서만 사용)
	NetworkConfig& Network() { return mNetwork; }
	TimeoutConfig& Timeout() { return mTimeout; }
	LogConfig& Log() { return mLog; }
	DBConfig& DB() { return mDB; }

	// English: Apply Docker-optimized defaults
	// 한글: Docker 최적화 기본값 적용
	void ApplyDockerDefaults();

	// English: Print current configuration to logger
	// 한글: 현재 설정을 로거에 출력
	void PrintConfig() const;

private:
	ConfigManager() = default;
	~ConfigManager() = default;
	ConfigManager(const ConfigManager&) = delete;
	ConfigManager& operator=(const ConfigManager&) = delete;

	// English: Helper: get environment variable
	// 한글: 도우미: 환경 변수 가져오기
	static std::string GetEnv(const char* name);

	NetworkConfig mNetwork;
	TimeoutConfig mTimeout;
	LogConfig mLog;
	DBConfig mDB;
};

} // namespace Network::Utils
