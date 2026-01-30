#pragma once

// English: Unified utility functions for NetworkModule
// 한글: NetworkModule용 통합 유틸리티 함수

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <queue>
#include <condition_variable>
#include <sstream>
#include <future>
#include <algorithm>
#include <iostream>
#include <cstring>

namespace Network::Utils
{
	// =============================================================================
	// English: Type definitions
	// 한글: 타입 정의
	// =============================================================================

	using NetworkHandle = uint64_t;
	using ConnectionId = uint64_t;
	using MessageId = uint32_t;
	using BufferSize = size_t;
	using Timestamp = uint64_t;

	// =============================================================================
	// English: Common constants
	// 한글: 공용 상수
	// =============================================================================

	constexpr uint32_t DEFAULT_PORT = 8000;
	constexpr size_t DEFAULT_BUFFER_SIZE = 4096;
	constexpr size_t MAX_CONNECTIONS = 10000;
	constexpr int DEFAULT_TIMEOUT_MS = 30000;
	constexpr Timestamp INVALID_TIMESTAMP = 0;

	// =============================================================================
	// English: Time utilities
	// 한글: 시간 유틸리티
	// =============================================================================

	class Timer
	{
	public:
		Timer() : mStartTime(GetCurrentTimestamp()) {}

		Timestamp GetElapsedTime() const
		{
			return GetCurrentTimestamp() - mStartTime;
		}

		void Reset()
		{
			mStartTime = GetCurrentTimestamp();
		}

		static Timestamp GetCurrentTimestamp()
		{
			auto now = std::chrono::system_clock::now();
			auto duration = now.time_since_epoch();
			return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
		}

	private:
		Timestamp mStartTime;
	};

	// =============================================================================
	// English: String utilities
	// 한글: 문자열 유틸리티
	// =============================================================================

	class StringUtils
	{
	public:
		static std::string Trim(const std::string& str)
		{
			size_t start = str.find_first_not_of(" \t\n\r");
			if (start == std::string::npos) return "";

			size_t end = str.find_last_not_of(" \t\n\r");
			return str.substr(start, end - start + 1);
		}

		static std::vector<std::string> Split(const std::string& str, char delimiter)
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

		static bool IsEmpty(const std::string& str)
		{
			return str.empty() || Trim(str).empty();
		}

		static std::string ToUpper(const std::string& str)
		{
			std::string result = str;
			std::transform(result.begin(), result.end(), result.begin(), ::toupper);
			return result;
		}

		static std::string ToLower(const std::string& str)
		{
			std::string result = str;
			std::transform(result.begin(), result.end(), result.begin(), ::tolower);
			return result;
		}
	};

	// =============================================================================
	// English: Buffer utilities
	// 한글: 버퍼 유틸리티
	// =============================================================================

	class BufferManager
	{
	public:
		BufferManager(size_t bufferSize = DEFAULT_BUFFER_SIZE)
			: mDefaultBufferSize(bufferSize)
		{
		}

		~BufferManager() = default;

		std::unique_ptr<uint8_t[]> Allocate(size_t size)
		{
			std::lock_guard<std::mutex> lock(mMutex);
			return std::make_unique<uint8_t[]>(size > 0 ? size : mDefaultBufferSize);
		}

		void Deallocate(uint8_t* /*buffer*/)
		{
			// English: No-op for unique_ptr-based allocation
			// 한글: unique_ptr 기반 할당에서는 아무것도 안 함
		}

		size_t GetPoolSize() const
		{
			return 0; // Not implemented
		}

		size_t GetUsedBuffers() const
		{
			return 0; // Not implemented
		}

	private:
		size_t mDefaultBufferSize;
		mutable std::mutex mMutex;
	};

	// =============================================================================
	// English: Thread-safe queue
	// 한글: 스레드 안전 큐
	// =============================================================================

	template<typename T>
	class SafeQueue
	{
	public:
		void Push(const T& item)
		{
			std::lock_guard<std::mutex> lock(mMutex);
			mQueue.push(item);
			mCondition.notify_one();
		}

		bool Pop(T& item, int timeoutMs = -1)
		{
			std::unique_lock<std::mutex> lock(mMutex);

			if (timeoutMs < 0)
			{
				mCondition.wait(lock, [this] { return !mQueue.empty() || mShutdown; });
			}
			else
			{
				if (!mCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
					[this] { return !mQueue.empty() || mShutdown; }))
				{
					return false; // Timeout
				}
			}

			if (mQueue.empty())
				return false;

			item = std::move(mQueue.front());
			mQueue.pop();
			return true;
		}

		bool Empty() const
		{
			std::lock_guard<std::mutex> lock(mMutex);
			return mQueue.empty();
		}

		size_t Size() const
		{
			std::lock_guard<std::mutex> lock(mMutex);
			return mQueue.size();
		}

		void Shutdown()
		{
			{
				std::lock_guard<std::mutex> lock(mMutex);
				mShutdown = true;
			}
			mCondition.notify_all();
		}

	private:
		std::queue<T> mQueue;
		mutable std::mutex mMutex;
		std::condition_variable mCondition;
		bool mShutdown = false;
	};

	// =============================================================================
	// English: Thread pool
	// 한글: 스레드 풀
	// =============================================================================

	class ThreadPool
	{
	public:
		ThreadPool(size_t numThreads = std::thread::hardware_concurrency())
			: mStop(false)
			, mActiveTasks(0)
		{
			if (numThreads == 0) numThreads = 4;

			for (size_t i = 0; i < numThreads; ++i)
			{
				mWorkers.emplace_back(&ThreadPool::WorkerThread, this);
			}
		}

		~ThreadPool()
		{
			mStop = true;
			mTasks.Shutdown();

			for (auto& worker : mWorkers)
			{
				if (worker.joinable())
				{
					worker.join();
				}
			}
		}

		// Submit task to thread pool
		template<typename F, typename... Args>
		auto Submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))>
		{
			using ReturnType = decltype(f(args...));

			auto task = std::make_shared<std::packaged_task<ReturnType()>>(
				std::bind(std::forward<F>(f), std::forward<Args>(args)...)
			);

			std::future<ReturnType> result = task->get_future();

			mTasks.Push([task]() { (*task)(); });

			return result;
		}

		void WaitForAll()
		{
			while (mActiveTasks > 0 || !mTasks.Empty())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}

		size_t GetThreadCount() const { return mWorkers.size(); }
		size_t GetActiveTaskCount() const { return mActiveTasks.load(); }

	private:
		std::vector<std::thread> mWorkers;
		SafeQueue<std::function<void()>> mTasks;
		std::atomic<bool> mStop;
		std::atomic<size_t> mActiveTasks;

		void WorkerThread()
		{
			while (!mStop)
			{
				std::function<void()> task;
				if (mTasks.Pop(task, 100))
				{
					++mActiveTasks;
					try
					{
						task();
					}
					catch (const std::exception& e)
					{
						// English: Swallow exception in worker thread
						// 한글: 워커 스레드에서 예외 삼킴
						(void)e;
					}
					--mActiveTasks;
				}
			}
		}
	};

	// =============================================================================
	// English: Configuration utilities
	// 한글: 설정 유틸리티
	// =============================================================================

	class ConfigManager
	{
	public:
		struct Config
		{
			uint16_t port = DEFAULT_PORT;
			size_t maxConnections = MAX_CONNECTIONS;
			size_t bufferSize = DEFAULT_BUFFER_SIZE;
			int timeoutMs = DEFAULT_TIMEOUT_MS;
			std::string logLevel = "INFO";
			std::string databaseHost = "localhost";
			uint16_t databasePort = 5432;
			std::string databaseName = "networkdb";
			std::string databaseUser = "postgres";
			std::string databasePassword = "password";
		};

		static Config LoadFromFile(const std::string& /*filename*/)
		{
			// English: Stub - returns default config
			// 한글: 스텁 - 기본 설정 반환
			return GetDefault();
		}

		static bool SaveToFile(const Config& /*config*/, const std::string& /*filename*/)
		{
			// English: Stub
			// 한글: 스텁
			return false;
		}

		static Config GetDefault()
		{
			return Config{};
		}

		static bool ValidateConfig(const Config& config)
		{
			return config.port > 0 && config.maxConnections > 0;
		}
	};

	// =============================================================================
	// English: Logging utilities
	// 한글: 로깅 유틸리티
	// =============================================================================

	enum class LogLevel : int
	{
		Debug = 0,
		Info = 1,
		Warn = 2,
		Err = 3
	};

	class Logger
	{
	public:
		static void SetLevel(LogLevel level)
		{
			sCurrentLevel.store(level);
		}

		static void SetLogFile(const std::string& filename)
		{
			std::lock_guard<std::mutex> lock(sMutex);
			sLogFile = filename;
		}

		template<typename... Args>
		static void Debug(const std::string& format, Args... /*args*/)
		{
			WriteLog(LogLevel::Debug, format);
		}

		template<typename... Args>
		static void Info(const std::string& format, Args... /*args*/)
		{
			WriteLog(LogLevel::Info, format);
		}

		template<typename... Args>
		static void Warn(const std::string& format, Args... /*args*/)
		{
			WriteLog(LogLevel::Warn, format);
		}

		template<typename... Args>
		static void Error(const std::string& format, Args... /*args*/)
		{
			WriteLog(LogLevel::Err, format);
		}

		static void Flush()
		{
			std::cout.flush();
		}

	private:
		static inline std::atomic<LogLevel> sCurrentLevel{ LogLevel::Info };
		static inline std::string sLogFile;
		static inline std::mutex sMutex;

		static void WriteLog(LogLevel level, const std::string& message)
		{
			if (static_cast<int>(level) < static_cast<int>(sCurrentLevel.load()))
			{
				return;
			}

			std::string formatted = FormatMessage(level, message);

			std::lock_guard<std::mutex> lock(sMutex);
			std::cout << formatted << std::endl;
		}

		static std::string FormatMessage(LogLevel level, const std::string& message)
		{
			const char* levelStr = "???";
			switch (level)
			{
			case LogLevel::Debug: levelStr = "DEBUG"; break;
			case LogLevel::Info:  levelStr = "INFO";  break;
			case LogLevel::Warn:  levelStr = "WARN";  break;
			case LogLevel::Err:   levelStr = "ERROR"; break;
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
