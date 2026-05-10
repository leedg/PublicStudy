// English: Configuration manager implementation
// Korean: 설정 관리자 구현

#include "ConfigManager.h"
#include "Logger.h"
#include "NetworkUtils.h"
#include <cstdlib>
#include <sstream>
#include <algorithm>

namespace Network::Utils
{

// =============================================================================
// English: Singleton instance
// Korean: 싱글톤 인스턴스
// =============================================================================

ConfigManager& ConfigManager::Instance()
{
	static ConfigManager instance;
	return instance;
}

// =============================================================================
// English: Initialize with defaults
// Korean: 기본값으로 초기화
// =============================================================================

void ConfigManager::Initialize()
{
	// English: Apply Docker defaults if running in container
	// 한글: 컨테이너에서 실행 중이면 Docker 기본값 적용
	if (TimeoutConfig::IsDocker)
	{
		ApplyDockerDefaults();
	}

	// English: Override with environment variables if present
	// 한글: 환경 변수가 있으면 덮어쓰기
	LoadFromEnv();
}

// =============================================================================
// English: Apply Docker-optimized defaults
// Korean: Docker 최적화 기본값 적용
// =============================================================================

void ConfigManager::ApplyDockerDefaults()
{
	mTimeout.ConnectTimeoutMs = 3000;
	mTimeout.RecvTimeoutMs = 15000;
	mTimeout.SendTimeoutMs = 5000;
	mTimeout.PingIntervalMs = 3000;
	mTimeout.PingTimeoutMs = 5000;
	mTimeout.GracefulShutdownTimeoutMs = 5000;

	mLog.EnableFile = true;
	mLog.Level = "DEBUG";

	Logger::Info("ConfigManager: Docker defaults applied (shorter timeouts, debug logging)");
}

// =============================================================================
// English: Load configuration from environment variables
// Korean: 환경 변수에서 설정 로드
// =============================================================================

void ConfigManager::LoadFromEnv()
{
	// English: Network settings
	// 한글: 네트워크 설정
	auto portStr = GetEnv("NETMOD_LISTEN_PORT");
	if (!portStr.empty())
	{
		mNetwork.ListenPort = static_cast<uint16_t>(std::stoi(portStr));
	}

	auto dbHost = GetEnv("NETMOD_DB_HOST");
	if (!dbHost.empty())
	{
		mNetwork.DBServerHost = dbHost;
	}

	auto dbPortStr = GetEnv("NETMOD_DB_PORT");
	if (!dbPortStr.empty())
	{
		mNetwork.DBServerPort = static_cast<uint16_t>(std::stoi(dbPortStr));
	}

	auto engineStr = GetEnv("NETMOD_ENGINE");
	if (!engineStr.empty())
	{
		mNetwork.EngineType = engineStr;
	}

	auto maxConnStr = GetEnv("NETMOD_MAX_CONNECTIONS");
	if (!maxConnStr.empty())
	{
		mNetwork.MaxConnections = static_cast<size_t>(std::stoul(maxConnStr));
	}

	auto workerStr = GetEnv("NETMOD_WORKER_THREADS");
	if (!workerStr.empty())
	{
		mNetwork.WorkerThreadCount = static_cast<uint32_t>(std::stoul(workerStr));
	}

	// English: Timeout settings
	// 한글: 타임아웃 설정
	auto shutdownStr = GetEnv("NETMOD_GRACEFUL_TIMEOUT");
	if (!shutdownStr.empty())
	{
		mTimeout.GracefulShutdownTimeoutMs = static_cast<uint32_t>(std::stoul(shutdownStr) * 1000);
	}

	auto pingIntervalStr = GetEnv("NETMOD_PING_INTERVAL");
	if (!pingIntervalStr.empty())
	{
		mTimeout.PingIntervalMs = static_cast<uint32_t>(std::stoul(pingIntervalStr));
	}

	// English: Logging settings
	// 한글: 로깅 설정
	auto logLevel = GetEnv("NETMOD_LOG_LEVEL");
	if (!logLevel.empty())
	{
		mLog.Level = StringUtils::ToUpper(logLevel);
	}

	auto logDir = GetEnv("NETMOD_LOG_DIR");
	if (!logDir.empty())
	{
		mLog.LogDir = logDir;
	}

	auto logFile = GetEnv("NETMOD_LOG_FILE");
	if (logFile == "1" || StringUtils::ToUpper(logFile) == "TRUE")
	{
		mLog.EnableFile = true;
	}

	// English: Database settings
	// 한글: 데이터베이스 설정
	auto dbConnStr = GetEnv("NETMOD_DB_CONNECTION");
	if (!dbConnStr.empty())
	{
		mDB.ConnectionString = dbConnStr;
	}

	auto dbPoolStr = GetEnv("NETMOD_DB_POOL_SIZE");
	if (!dbPoolStr.empty())
	{
		mDB.PoolSize = static_cast<uint32_t>(std::stoul(dbPoolStr));
	}
}

// =============================================================================
// English: Helper: get environment variable
// Korean: 도우미: 환경 변수 가져오기
// =============================================================================

std::string ConfigManager::GetEnv(const char* name)
{
#ifdef _WIN32
	char buffer[512];
	DWORD len = GetEnvironmentVariableA(name, buffer, sizeof(buffer));
	if (len == 0 || len >= sizeof(buffer))
		return "";
	return std::string(buffer, len);
#else
	const char* value = std::getenv(name);
	return value ? std::string(value) : "";
#endif
}

// =============================================================================
// English: Print current configuration
// Korean: 현재 설정 출력
// =============================================================================

void ConfigManager::PrintConfig() const
{
	Logger::Info("=== Configuration ===");
	Logger::Info("Network:");
	Logger::Info("  Listen Port     : " + std::to_string(mNetwork.ListenPort));
	Logger::Info("  DB Host         : " + mNetwork.DBServerHost);
	Logger::Info("  DB Port         : " + std::to_string(mNetwork.DBServerPort));
	Logger::Info("  Engine          : " + mNetwork.EngineType);
	Logger::Info("  Max Connections : " + std::to_string(mNetwork.MaxConnections));
	Logger::Info("  Worker Threads  : " + (mNetwork.WorkerThreadCount > 0 ? std::to_string(mNetwork.WorkerThreadCount) : "auto"));

	Logger::Info("Timeouts:");
	Logger::Info("  Connect         : " + std::to_string(mTimeout.ConnectTimeoutMs) + "ms");
	Logger::Info("  Recv            : " + std::to_string(mTimeout.RecvTimeoutMs) + "ms");
	Logger::Info("  Ping Interval   : " + std::to_string(mTimeout.PingIntervalMs) + "ms");
	Logger::Info("  Graceful Shutdown: " + std::to_string(mTimeout.GracefulShutdownTimeoutMs) + "ms");

	Logger::Info("Logging:");
	Logger::Info("  Level           : " + mLog.Level);
	Logger::Info("  Dir             : " + mLog.LogDir);
	Logger::Info("  File            : " + std::string(mLog.EnableFile ? "enabled" : "disabled"));

	if (!mDB.ConnectionString.empty())
	{
		Logger::Info("Database:");
		Logger::Info("  Connection String: " + mDB.ConnectionString);
		Logger::Info("  Pool Size       : " + std::to_string(mDB.PoolSize));
	}
	Logger::Info("====================");
}

} // namespace Network::Utils
