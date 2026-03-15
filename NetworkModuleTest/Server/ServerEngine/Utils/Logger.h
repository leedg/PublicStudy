#pragma once

// Logging utility

#include <atomic>
#include <mutex>
#include <string>
#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstring>
#include <memory>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Network::Utils
{
// =============================================================================
// Log levels
// =============================================================================

enum class LogLevel : int
{
	Debug = 0,
	Info = 1,
	Warn = 2,
	Err = 3
};

// =============================================================================
// Logger - provides logging functionality with levels
// =============================================================================

class Logger
{
public:
	// Set minimum log level
	static void SetLevel(LogLevel level) { sCurrentLevel.store(level); }

	// Set log file path and open file for writing
	static void SetLogFile(const std::string &filename)
	{
		std::lock_guard<std::mutex> lock(sMutex);
		sLogFile = filename;
		
		// Open log file in append mode
		if (!filename.empty())
		{
			sLogFileStream = std::make_unique<std::ofstream>(filename, std::ios::app);
			if (!sLogFileStream->is_open())
			{
				sLogFileStream.reset();
				std::cerr << "[Logger] Failed to open log file: " << filename << std::endl;
			}
		}
		else
		{
			sLogFileStream.reset();
		}
	}

	// Log debug message
	template <typename... Args>
	static void Debug(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Debug, format);
	}

	// Log info message
	template <typename... Args>
	static void Info(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Info, format);
	}

	// Log warning message
	template <typename... Args>
	static void Warn(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Warn, format);
	}

	// Log error message
	template <typename... Args>
	static void Error(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Err, format);
	}

	// Flush output buffer
	static void Flush() { std::cout.flush(); }

private:
	static inline std::atomic<LogLevel> sCurrentLevel{LogLevel::Info};
	static inline std::string sLogFile;
	static inline std::unique_ptr<std::ofstream> sLogFileStream;
	static inline std::mutex sMutex;
	static inline std::atomic<bool> sConsoleInitialized{false};

	// Initialize console for UTF-8 output (Korean support)
	static void InitConsoleUTF8()
	{
		if (sConsoleInitialized.exchange(true))
		{
			return; // Already initialized
		}

#ifdef _WIN32
		// Set console code page to UTF-8 for Korean output
		SetConsoleCP(65001);
		SetConsoleOutputCP(65001);
#endif
	}

	// Write log message with level check to console and file
	static void WriteLog(LogLevel level, const std::string &message)
	{
		// Ensure console is initialized for UTF-8 on first use
		InitConsoleUTF8();

		if (static_cast<int>(level) < static_cast<int>(sCurrentLevel.load()))
		{
			return;
		}

		std::string formatted = FormatMessage(level, message);

		std::lock_guard<std::mutex> lock(sMutex);
		
		// Write to console
		std::cout << formatted << std::endl;
		
		// Write to file if available
		if (sLogFileStream && sLogFileStream->is_open())
		{
			*sLogFileStream << formatted << std::endl;
			sLogFileStream->flush();
		}
	}

	// Format log message with timestamp and level (optimized)
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

		// Use string reserve and append to avoid multiple allocations
		std::string result;
		result.reserve(64 + message.size());
		result.append("[").append(timeStr).append("] [").append(levelStr).append("] ").append(message);
		return result;
	}
};

} // namespace Network::Utils
