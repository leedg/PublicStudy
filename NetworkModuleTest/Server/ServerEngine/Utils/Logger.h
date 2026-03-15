#pragma once

// English: Logging utility
// 한글: 로깅 유틸리티

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

	// English: Set log file path and open file for writing
	// 한글: 로그 파일 경로 설정 및 쓰기용 파일 열기
	static void SetLogFile(const std::string &filename)
	{
		std::lock_guard<std::mutex> lock(sMutex);
		sLogFile = filename;
		
		// English: Open log file in append mode
		// 한글: 추가 모드로 로그 파일 열기
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
	static inline std::unique_ptr<std::ofstream> sLogFileStream;
	static inline std::mutex sMutex;
	static inline std::atomic<bool> sConsoleInitialized{false};

	// English: Initialize console for UTF-8 output (Korean support)
	// 한글: 콘솔 UTF-8 출력 초기화 (한글 지원)
	static void InitConsoleUTF8()
	{
		if (sConsoleInitialized.exchange(true))
		{
			return; // English: Already initialized / 한글: 이미 초기화됨
		}

#ifdef _WIN32
		// English: Set console code page to UTF-8 for Korean output
		// 한글: 한글 출력을 위해 콘솔 코드 페이지를 UTF-8로 설정
		SetConsoleCP(65001);
		SetConsoleOutputCP(65001);
#endif
	}

	// English: Write log message with level check to console and file
	// 한글: 레벨 확인 후 콘솔과 파일에 로그 메시지 작성
	static void WriteLog(LogLevel level, const std::string &message)
	{
		// English: Ensure console is initialized for UTF-8 on first use
		// 한글: 최초 사용 시 콘솔 UTF-8 초기화 보장
		InitConsoleUTF8();

		if (static_cast<int>(level) < static_cast<int>(sCurrentLevel.load()))
		{
			return;
		}

		std::string formatted = FormatMessage(level, message);

		std::lock_guard<std::mutex> lock(sMutex);
		
		// English: Write to console
		// 한글: 콘솔에 작성
		std::cout << formatted << std::endl;
		
		// English: Write to file if available
		// 한글: 파일이 있으면 파일에도 작성
		if (sLogFileStream && sLogFileStream->is_open())
		{
			*sLogFileStream << formatted << std::endl;
			sLogFileStream->flush();
		}
	}

	// English: Format log message with timestamp and level (optimized)
	// 한글: 타임스탬프와 레벨로 로그 메시지 포맷 (최적화)
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

		// English: Use string reserve and append to avoid multiple allocations
		// 한글: 여러 할당을 피하기 위해 string reserve 및 append 사용
		std::string result;
		result.reserve(64 + message.size());
		result.append("[").append(timeStr).append("] [").append(levelStr).append("] ").append(message);
		return result;
	}
};

} // namespace Network::Utils
