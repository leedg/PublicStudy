#pragma once

// 로깅 유틸리티.
//
// 스레드 안전성:
//   - WriteLog()는 내부 sMutex로 콘솔/파일 출력을 직렬화한다.
//   - SetLevel()은 atomic 갱신이므로 락 없이 호출 가능.
//   - SetLogFile()은 sMutex를 획득한 뒤 파일 스트림을 교체한다.
//
// 출력 포맷: [HH:MM:SS] [LEVEL] <message>
//   예시 → [14:03:27] [INFO] Server started on port 9000
//
// 로그 레벨 (오름차순): Debug(0) < Info(1) < Warn(2) < Err(3)
//   SetLevel(LogLevel::Warn) 이후엔 Debug/Info 출력이 억제된다.

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
// 로그 레벨 — 낮을수록 상세, 높을수록 심각
// =============================================================================

enum class LogLevel : int
{
	Debug = 0,
	Info = 1,
	Warn = 2,
	Err = 3
};

// =============================================================================
// Logger — 레벨 필터링 + 콘솔/파일 동시 출력을 지원하는 정적 로거.
//
// 주의: 파일 출력 활성화 시 매 메시지마다 flush()가 호출되므로
//       고빈도 로그 경로에서는 SetLevel로 불필요한 레벨을 차단할 것.
// =============================================================================

class Logger
{
public:
	// 최소 로그 레벨 설정 (atomic — 락 없이 호출 가능)
	static void SetLevel(LogLevel level) { sCurrentLevel.store(level); }

	// 로그 파일 경로 설정. 빈 문자열을 전달하면 파일 출력을 닫는다.
	// 파일은 추가(append) 모드로 열린다.
	static void SetLogFile(const std::string &filename)
	{
		std::lock_guard<std::mutex> lock(sMutex);
		sLogFile = filename;

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

	template <typename... Args>
	static void Debug(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Debug, format);
	}

	template <typename... Args>
	static void Info(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Info, format);
	}

	template <typename... Args>
	static void Warn(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Warn, format);
	}

	template <typename... Args>
	static void Error(const std::string &format, Args... /*args*/)
	{
		WriteLog(LogLevel::Err, format);
	}

	static void Flush() { std::cout.flush(); }

private:
	static inline std::atomic<LogLevel> sCurrentLevel{LogLevel::Info};
	static inline std::string sLogFile;
	static inline std::unique_ptr<std::ofstream> sLogFileStream;
	static inline std::mutex sMutex;
	static inline std::atomic<bool> sConsoleInitialized{false};

	// Windows 콘솔 코드 페이지를 UTF-8로 초기화 (한글 출력 지원).
	// call_once 대신 atomic exchange로 중복 초기화를 방지한다.
	static void InitConsoleUTF8()
	{
		if (sConsoleInitialized.exchange(true))
		{
			return; // 이미 초기화됨
		}

#ifdef _WIN32
		SetConsoleCP(65001);
		SetConsoleOutputCP(65001);
#endif
	}

	// 레벨 필터 통과 후 콘솔과 파일에 동시 출력.
	// sMutex로 직렬화하므로 다중 스레드에서 호출해도 출력이 섞이지 않는다.
	static void WriteLog(LogLevel level, const std::string &message)
	{
		InitConsoleUTF8();

		if (static_cast<int>(level) < static_cast<int>(sCurrentLevel.load()))
		{
			return;
		}

		std::string formatted = FormatMessage(level, message);

		std::lock_guard<std::mutex> lock(sMutex);
		std::cout << formatted << std::endl;

		if (sLogFileStream && sLogFileStream->is_open())
		{
			*sLogFileStream << formatted << std::endl;
			sLogFileStream->flush();
		}
	}

	// 포맷: [HH:MM:SS] [LEVEL] <message>
	// string::reserve + append 조합으로 불필요한 재할당을 최소화한다.
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

		std::string result;
		result.reserve(64 + message.size());
		result.append("[").append(timeStr).append("] [").append(levelStr).append("] ").append(message);
		return result;
	}
};

} // namespace Network::Utils
