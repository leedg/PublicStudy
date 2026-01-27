// AsyncIO Test - Simple verification (no GTest dependency)
#include "AsyncIOProvider.h"
#include <memory>
#include <iostream>

using namespace Network::AsyncIO;

// =============================================================================
// Simple Test Functions
// =============================================================================

void TestPlatformDetection()
{
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

void TestAsyncIOProviderCreation()
{
    std::cout << "\n=== AsyncIOProvider Creation Test ===" << std::endl;
    
    auto provider = CreateAsyncIOProvider();
    
    if (provider)
    {
        std::cout << "[PASS] Provider created successfully" << std::endl;
        
        if (provider->Initialize(1000))
        {
            std::cout << "[PASS] Provider initialized successfully" << std::endl;
            
            auto platformInfo = provider->GetPlatformInfo();
            std::cout << "Platform: " << platformInfo.osName << std::endl;
            std::cout << "Version: " << (int)platformInfo.majorVersion << "." 
                      << (int)platformInfo.minorVersion << std::endl;
            
            provider->Shutdown();
            std::cout << "[PASS] Provider shutdown successfully" << std::endl;
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

void TestAsyncIOProviderWithHighPerformance()
{
    std::cout << "\n=== AsyncIOProvider Creation (High Performance) ===" << std::endl;
    
    auto provider = CreateAsyncIOProvider(true);  // Prefer high performance
    
    if (provider)
    {
        std::cout << "[PASS] High-performance provider created" << std::endl;
        
        if (provider->Initialize(1000))
        {
            std::cout << "[PASS] Provider initialized" << std::endl;
            
            // Check supported features
            if (provider->SupportsFeature("SendAsync"))
            {
                std::cout << "[PASS] SendAsync supported" << std::endl;
            }
            
            if (provider->SupportsFeature("RecvAsync"))
            {
                std::cout << "[PASS] RecvAsync supported" << std::endl;
            }
            
            provider->Shutdown();
        }
        else
        {
            std::cout << "[FAIL] Provider initialization failed" << std::endl;
        }
    }
    else
    {
        std::cout << "[FAIL] Failed to create high-performance provider" << std::endl;
    }
}

void TestPlatformSpecificProviders()
{
    std::cout << "\n=== Platform-Specific Provider Tests ===" << std::endl;

#ifdef _WIN32
    // Test IOCP provider
    {
        auto provider = CreateAsyncIOProviderForPlatform(PlatformType::IOCP);
        if (provider && provider->Initialize(1000))
        {
            std::cout << "[PASS] IOCP provider created and initialized" << std::endl;
            provider->Shutdown();
        }
        else
        {
            std::cout << "[FAIL] IOCP provider failed" << std::endl;
        }
    }
    
    // Test RIO provider (if available)
    {
        auto provider = CreateAsyncIOProviderForPlatform(PlatformType::RIO);
        if (provider)
        {
            if (provider->Initialize(1000))
            {
                std::cout << "[PASS] RIO provider created and initialized" << std::endl;
                provider->Shutdown();
            }
            else
            {
                std::cout << "[INFO] RIO provider not available on this system" << std::endl;
            }
        }
        else
        {
            std::cout << "[INFO] RIO provider not available" << std::endl;
        }
    }
#endif

#ifdef __linux__
    // Test epoll provider
    {
        auto provider = CreateAsyncIOProviderForPlatform(PlatformType::Epoll);
        if (provider && provider->Initialize(1000))
        {
            std::cout << "[PASS] epoll provider created and initialized" << std::endl;
            provider->Shutdown();
        }
        else
        {
            std::cout << "[FAIL] epoll provider failed" << std::endl;
        }
    }
    
    // Test io_uring provider (if available)
    {
        auto provider = CreateAsyncIOProviderForPlatform(PlatformType::IOUring);
        if (provider)
        {
            if (provider->Initialize(1000))
            {
                std::cout << "[PASS] io_uring provider created and initialized" << std::endl;
                provider->Shutdown();
            }
            else
            {
                std::cout << "[INFO] io_uring not available on this system" << std::endl;
            }
        }
        else
        {
            std::cout << "[INFO] io_uring provider not available" << std::endl;
        }
    }
#endif

#ifdef __APPLE__
    // Test kqueue provider
    {
        auto provider = CreateAsyncIOProviderForPlatform(PlatformType::Kqueue);
        if (provider && provider->Initialize(1000))
        {
            std::cout << "[PASS] kqueue provider created and initialized" << std::endl;
            provider->Shutdown();
        }
        else
        {
            std::cout << "[FAIL] kqueue provider failed" << std::endl;
        }
    }
#endif
}

// =============================================================================
// Main Entry Point
// =============================================================================

int main(int argc, char* argv[])
{
    std::cout << "====================================" << std::endl;
    std::cout << "AsyncIO Provider Test Suite" << std::endl;
    std::cout << "====================================" << std::endl;

    try
    {
        TestPlatformDetection();
        TestAsyncIOProviderCreation();
        TestAsyncIOProviderWithHighPerformance();
        TestPlatformSpecificProviders();

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
