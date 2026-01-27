// English: AsyncIO Provider Test Suite - Simple verification (no GTest dependency)
// 한글: AsyncIO 공급자 테스트 - 간단한 검증 (GTest 의존성 없음)

#include "AsyncIOProvider.h"
#include <memory>
#include <iostream>

using namespace Network::AsyncIO;

// =============================================================================
// English: Test Functions
// 한글: 테스트 함수
// =============================================================================

void TestPlatformDetection()
{
    // English: Test platform detection
    // 한글: 플랫폼 감지 테스트
    std::cout << "=== Platform Detection Test ===" << std::endl;

    PlatformType platform = GetCurrentPlatform();

#ifdef _WIN32
    std::cout << "Current Platform: Windows (IOCP/RIO)" << std::endl;
    if (platform == PlatformType::IOCP || platform == PlatformType::RIO)
    {
        std::cout << "[PASS] Platform detected correctly" << std::endl;
    }
    else
    {
        std::cout << "[FAIL] Unexpected platform type" << std::endl;
    }
#elif __linux__
    std::cout << "Current Platform: Linux (epoll/io_uring)" << std::endl;
    if (platform == PlatformType::Epoll || platform == PlatformType::IOUring)
    {
        std::cout << "[PASS] Platform detected correctly" << std::endl;
    }
    else
    {
        std::cout << "[FAIL] Unexpected platform type" << std::endl;
    }
#elif __APPLE__
    std::cout << "Current Platform: macOS (kqueue)" << std::endl;
    if (platform == PlatformType::Kqueue)
    {
        std::cout << "[PASS] Platform detected correctly" << std::endl;
    }
    else
    {
        std::cout << "[FAIL] Unexpected platform type" << std::endl;
    }
#else
    std::cout << "[FAIL] Unknown platform" << std::endl;
#endif
}

void TestPlatformSupport()
{
    // English: Test IsPlatformSupported and GetSupportedPlatforms
    // 한글: IsPlatformSupported 및 GetSupportedPlatforms 테스트
    std::cout << "\n=== Platform Support Test ===" << std::endl;

    // English: Get supported platforms list
    // 한글: 지원 플랫폼 목록 조회
    size_t count = 0;
    const char** platforms = GetSupportedPlatforms(count);

    std::cout << "Supported platforms (" << count << "):" << std::endl;
    for (size_t i = 0; i < count; ++i)
    {
        bool supported = IsPlatformSupported(platforms[i]);
        std::cout << "  " << platforms[i] << ": "
                  << (supported ? "available" : "not available") << std::endl;
    }

    std::cout << "[PASS] Platform support query completed" << std::endl;
}

void TestAsyncIOProviderCreation()
{
    // English: Test automatic provider creation
    // 한글: 자동 공급자 생성 테스트
    std::cout << "\n=== AsyncIOProvider Creation Test ===" << std::endl;

    // English: Create with automatic platform selection
    // 한글: 자동 플랫폼 선택으로 생성
    auto provider = CreateAsyncIOProvider();

    if (provider)
    {
        std::cout << "[PASS] Provider created successfully" << std::endl;

        // English: Initialize with doc-specified interface
        // 한글: 문서 사양 인터페이스로 초기화
        AsyncIOError err = provider->Initialize(256, 1000);
        if (err == AsyncIOError::Success)
        {
            std::cout << "[PASS] Provider initialized successfully" << std::endl;

            // English: Check IsInitialized
            // 한글: IsInitialized 확인
            if (provider->IsInitialized())
            {
                std::cout << "[PASS] IsInitialized returns true" << std::endl;
            }

            // English: Check GetInfo
            // 한글: GetInfo 확인
            const ProviderInfo& info = provider->GetInfo();
            std::cout << "Backend: " << info.mName << std::endl;
            std::cout << "Buffer Registration: " << (info.mSupportsBufferReg ? "yes" : "no") << std::endl;
            std::cout << "Batching: " << (info.mSupportsBatching ? "yes" : "no") << std::endl;

            // English: Check GetStats
            // 한글: GetStats 확인
            ProviderStats stats = provider->GetStats();
            std::cout << "Total Requests: " << stats.mTotalRequests << std::endl;

            // English: Check GetLastError
            // 한글: GetLastError 확인
            const char* lastErr = provider->GetLastError();
            std::cout << "Last Error: \"" << (lastErr ? lastErr : "") << "\"" << std::endl;

            // English: Test FlushRequests (should be no-op or success)
            // 한글: FlushRequests 테스트 (no-op 또는 성공이어야 함)
            AsyncIOError flushErr = provider->FlushRequests();
            if (flushErr == AsyncIOError::Success)
            {
                std::cout << "[PASS] FlushRequests succeeded" << std::endl;
            }

            provider->Shutdown();
            std::cout << "[PASS] Provider shutdown successfully" << std::endl;

            // English: Verify IsInitialized after shutdown
            // 한글: 종료 후 IsInitialized 확인
            if (!provider->IsInitialized())
            {
                std::cout << "[PASS] IsInitialized returns false after shutdown" << std::endl;
            }
        }
        else
        {
            std::cout << "[FAIL] Provider initialization failed" << std::endl;
        }
    }
    else
    {
        std::cout << "[FAIL] Failed to create provider" << std::endl;
    }
}

void TestNamedProviderCreation()
{
    // English: Test named provider creation (CreateAsyncIOProvider with platformHint)
    // 한글: 이름 기반 공급자 생성 테스트
    std::cout << "\n=== Named Provider Creation Test ===" << std::endl;

#ifdef _WIN32
    auto iocpProvider = CreateAsyncIOProvider("IOCP");
    if (iocpProvider)
    {
        std::cout << "[PASS] IOCP provider created by name" << std::endl;
    }
    else
    {
        std::cout << "[FAIL] IOCP provider creation by name failed" << std::endl;
    }
#elif __linux__
    auto epollProvider = CreateAsyncIOProvider("epoll");
    if (epollProvider)
    {
        std::cout << "[PASS] epoll provider created by name" << std::endl;
    }
    else
    {
        std::cout << "[FAIL] epoll provider creation by name failed" << std::endl;
    }
#elif __APPLE__
    auto kqueueProvider = CreateAsyncIOProvider("kqueue");
    if (kqueueProvider)
    {
        std::cout << "[PASS] kqueue provider created by name" << std::endl;
    }
    else
    {
        std::cout << "[FAIL] kqueue provider creation by name failed" << std::endl;
    }
#endif

    // English: Test unsupported platform name
    // 한글: 지원되지 않는 플랫폼 이름 테스트
    auto nullProvider = CreateAsyncIOProvider("nonexistent");
    if (!nullProvider)
    {
        std::cout << "[PASS] Unsupported platform returns nullptr" << std::endl;
    }
}

// =============================================================================
// English: Main Entry Point
// 한글: 메인 진입점
// =============================================================================

int main(int argc, char* argv[])
{
    std::cout << "====================================" << std::endl;
    std::cout << "AsyncIO Provider Test Suite" << std::endl;
    std::cout << "====================================" << std::endl;

    try
    {
        TestPlatformDetection();
        TestPlatformSupport();
        TestAsyncIOProviderCreation();
        TestNamedProviderCreation();

        std::cout << "\n====================================" << std::endl;
        std::cout << "All tests completed" << std::endl;
        std::cout << "====================================" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Test exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
