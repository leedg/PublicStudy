#pragma once

// English: Logging utility
// 한글: 로깅 유틸리티

#include <atomic>
#include <mutex>
#include <string>
#include <iostream>
#include <chrono>
#include <ctime>
#include <cstring>

namespace Network::Utils
{
// =============================================================================
// English: Log levels
// 한글: 로그 레벨
// =============================================================================

enum class LogLevel : int
{
	Debug = 0,
	Info = 1,
	Warn = 2,
	Err = 3
};

// =============================================================================
// English: Logger - provides logging functionality with levels
// 한글: Logger - 레벨별 로깅 기능 제공
// =============================================================================

class Logger
{
public:
	// English: Set minimum log level
	// 한글: 최소 로그 레벨 설정
	static void SetLevel(LogLevel level) { sCurrentLevel.store(level); }

	// English: Set log file path (not implemented)
	// 한글: 로그 파일 경로 설정 (미구현)
	static void SetLogFile(const std::string &filename)
	{
		std::lock_guard<std::mutex> lock(sMutex);
		sLogFile = filename;
	}

	// English: Log debug message
	// 한글: 디버그 메시지 로깅
	template <typename... Args>
	static void Debug(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Debug, format);
	}

	// English: Log info message
	// 한글: 정보 메시지 로깅
	template <typename... Args>
	static void Info(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Info, format);
	}

	// English: Log warning message
	// 한글: 경고 메시지 로깅
	template <typename... Args>
	static void Warn(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Warn, format);
	}

	// English: Log error message
	// 한글: 오류 메시지 로깅
	template <typename... Args>
	static void Error(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Err, format);
	}

	// English: Flush output buffer
	// 한글: 출력 버퍼 플러시
	static void Flush() { std::cout.flush(); }

private:
	static inline std::atomic<LogLevel> sCurrentLevel{LogLevel::Info};
	static inline std::string sLogFile;
	static inline std::mutex sMutex;

	// English: Write log message with level check
	// 한글: 레벨 확인 후 로그 메시지 작성
	static void WriteLog(LogLevel level, const std::string &message)
	{
		if (static_cast<int>(level) < static_cast<int>(sCurrentLevel.load()))
		{
			return;
		}

		std::string formatted = FormatMessage(level, message);

		std::lock_guard<std::mutex> lock(sMutex);
		std::cout << formatted << std::endl;
	}

	// English: Format log message with timestamp and level
	// 한글: 타임스탬프와 레벨로 로그 메시지 포맷
	static std::string FormatMessage(LogLevel level, const std::string &message)
	{
		const char *levelStr = "???";
		switch (level)
		{
		case LogLevel::Debug:
			levelStr = "DEBUG";
			break;
		case LogLevel::Info:
			levelStr = "INFO";
			break;
		case LogLevel::Warn:
			levelStr = "WARN";
			break;
		case LogLevel::Err:
			levelStr = "ERROR";
			break;
		}

		auto now = std::chrono::system_clock::now();
		auto time = std::chrono::system_clock::to_time_t(now);

		char timeStr[32];
		std::tm localTime;
#ifdef _WIN32
		localtime_s(&localTime, &time);
#else
		localtime_r(&time, &localTime);
#endif
		std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &localTime);

		return std::string("[") + timeStr + "] [" + levelStr + "] " + message;
	}
};

} // namespace Network::Utils
