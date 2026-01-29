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
        BufferManager(size_t bufferSize = DEFAULT_BUFFER_SIZE);
        ~BufferManager();
        
        // Allocate buffer
        std::unique_ptr<uint8_t[]> Allocate(size_t size);
        
        // Deallocate buffer
        void Deallocate(uint8_t* buffer);
        
        // Get buffer pool statistics
        size_t GetPoolSize() const;
        size_t GetUsedBuffers() const;
        
    private:
        struct BufferInfo
        {
            std::unique_ptr<uint8_t[]> buffer;
            bool inUse;
            Timestamp allocatedAt;
        };
        
        std::vector<BufferInfo> mBuffers;
        mutable std::mutex mMutex;
        size_t mDefaultBufferSize;
        
        std::unique_ptr<uint8_t[]> CreateNewBuffer(size_t size);
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
                mCondition.wait(lock, [this] { return !mQueue.empty(); });
            }
            else
            {
                if (!mCondition.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                       [this] { return !mQueue.empty(); }))
                {
                    return false; // Timeout
                }
            }
            
            if (mQueue.empty())
                return false;
                
            item = mQueue.front();
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
        
    private:
        std::queue<T> mQueue;
        mutable std::mutex mMutex;
        std::condition_variable mCondition;
    };
    
    // =============================================================================
    // English: Thread pool
    // 한글: 스레드 풀
    // =============================================================================
    
    class ThreadPool
    {
    public:
        ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
        ~ThreadPool();
        
        // Submit task to thread pool
        template<typename F, typename... Args>
        auto Submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))>;
        
        // Wait for all tasks to complete
        void WaitForAll();
        
        // Get thread pool statistics
        size_t GetThreadCount() const;
        size_t GetActiveTaskCount() const;
        
    private:
        std::vector<std::thread> mWorkers;
        SafeQueue<std::function<void()>> mTasks;
        std::atomic<bool> mStop;
        std::atomic<size_t> mActiveTasks;
        
        void WorkerThread();
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
        
        static Config LoadFromFile(const std::string& filename);
        static bool SaveToFile(const Config& config, const std::string& filename);
        static Config GetDefault();
        
        static bool ValidateConfig(const Config& config);
    };
    
    // =============================================================================
    // English: Logging utilities
    // 한글: 로깅 유틸리티
    // =============================================================================
    
    enum class LogLevel : int
    {
        DEBUG = 0,
        INFO = 1,
        WARN = 2,
        ERROR = 3
    };
    
    class Logger
    {
    public:
        static void SetLevel(LogLevel level);
        static void SetLogFile(const std::string& filename);
        
        template<typename... Args>
        static void Debug(const std::string& format, Args... args);
        
        template<typename... Args>
        static void Info(const std::string& format, Args... args);
        
        template<typename... Args>
        static void Warn(const std::string& format, Args... args);
        
        template<typename... Args>
        static void Error(const std::string& format, Args... args);
        
        static void Flush();
        
    private:
        static std::atomic<LogLevel> sCurrentLevel;
        static std::string sLogFile;
        static std::mutex sMutex;
        
        static void WriteLog(LogLevel level, const std::string& message);
        static std::string FormatMessage(LogLevel level, const std::string& message);
    };
    
} // namespace Network::Utils