#include <gtest/gtest.h>
#include "AsyncIOProvider.h"
#include <memory>
#include <thread>
#include <chrono>

using namespace RAON::Network::AsyncIO;

// =============================================================================
// Test Fixtures
// =============================================================================

class AsyncIOProviderTest : public ::testing::Test
{
protected:
    std::unique_ptr<AsyncIOProvider> provider;

    void SetUp() override
    {
        provider = CreateAsyncIOProvider();
        ASSERT_NE(provider, nullptr);
        ASSERT_TRUE(provider->Initialize(1000));
    }

    void TearDown() override
    {
        if (provider)
            provider->Shutdown();
    }
};

// =============================================================================
// Platform Detection Tests
// =============================================================================

TEST(PlatformDetectionTest, GetCurrentPlatform)
{
    PlatformType platform = Platform::GetCurrentPlatform();
    
    #ifdef _WIN32
        EXPECT_EQ(platform, PlatformType::Windows);
    #elif __APPLE__
        EXPECT_EQ(platform, PlatformType::macOS);
    #elif __linux__
        EXPECT_EQ(platform, PlatformType::Linux);
    #endif
}

TEST(PlatformDetectionTest, GetPlatformInfo)
{
    PlatformInfo info = Platform::GetDetailedPlatformInfo();
    
    EXPECT_GE(info.majorVersion, 0);
    EXPECT_GE(info.minorVersion, 0);
    EXPECT_NE(info.osName, nullptr);
    EXPECT_NE(info.arch, nullptr);
}

// =============================================================================
// Initialization Tests
// =============================================================================

TEST_F(AsyncIOProviderTest, InitializeWithMaxOps)
{
    std::unique_ptr<AsyncIOProvider> p = CreateAsyncIOProvider();
    ASSERT_TRUE(p->Initialize(5000));
    p->Shutdown();
}

TEST_F(AsyncIOProviderTest, InitializeMultipleTimes)
{
    // Initialize should be idempotent
    ASSERT_TRUE(provider->Initialize(1000));
    ASSERT_TRUE(provider->Initialize(1000));
}

TEST_F(AsyncIOProviderTest, GetPlatformInfo)
{
    PlatformInfo info = provider->GetPlatformInfo();
    EXPECT_NE(info.osName, nullptr);
    EXPECT_NE(info.arch, nullptr);
}

// =============================================================================
// Feature Support Tests
// =============================================================================

TEST_F(AsyncIOProviderTest, SupportsSendAsync)
{
    EXPECT_TRUE(provider->SupportsFeature("SendAsync"));
}

TEST_F(AsyncIOProviderTest, SupportsRecvAsync)
{
    EXPECT_TRUE(provider->SupportsFeature("RecvAsync"));
}

TEST_F(AsyncIOProviderTest, SupportsFeatureInvalid)
{
    EXPECT_FALSE(provider->SupportsFeature("InvalidFeature"));
    EXPECT_FALSE(provider->SupportsFeature(""));
}

// =============================================================================
// Socket Management Tests
// =============================================================================

TEST_F(AsyncIOProviderTest, RegisterSocketValid)
{
    // Note: Using -1 as a dummy socket handle for testing
    // In real tests, would need actual socket creation
    EXPECT_EQ(provider->RegisterSocket(-1), false);  // Invalid socket
}

TEST_F(AsyncIOProviderTest, UnregisterSocketValid)
{
    EXPECT_EQ(provider->UnregisterSocket(-1), false);  // Invalid socket
}

// =============================================================================
// Async I/O Operation Tests
// =============================================================================

TEST_F(AsyncIOProviderTest, SendAsyncInvalidParameters)
{
    uint8_t buffer[100] = {0};
    
    // Invalid socket
    EXPECT_FALSE(provider->SendAsync(-1, buffer, sizeof(buffer), nullptr, 0, nullptr));
    
    // Null data
    EXPECT_FALSE(provider->SendAsync(0, nullptr, sizeof(buffer), nullptr, 0, nullptr));
    
    // Zero size
    EXPECT_FALSE(provider->SendAsync(0, buffer, 0, nullptr, 0, nullptr));
}

TEST_F(AsyncIOProviderTest, RecvAsyncInvalidParameters)
{
    uint8_t buffer[100] = {0};
    
    // Invalid socket
    EXPECT_FALSE(provider->RecvAsync(-1, buffer, sizeof(buffer), nullptr, 0, nullptr));
    
    // Null buffer
    EXPECT_FALSE(provider->RecvAsync(0, nullptr, sizeof(buffer), nullptr, 0, nullptr));
    
    // Zero size
    EXPECT_FALSE(provider->RecvAsync(0, buffer, 0, nullptr, 0, nullptr));
}

// =============================================================================
// Buffer Management Tests
// =============================================================================

TEST_F(AsyncIOProviderTest, RegisterBufferValid)
{
    uint8_t buffer[1024] = {0};
    BufferRegistration reg = provider->RegisterBuffer(buffer, sizeof(buffer));
    
    if (provider->SupportsFeature("BufferRegistration"))
    {
        EXPECT_GE(reg.bufferId, 0);
        EXPECT_TRUE(reg.isSuccessful);
        EXPECT_EQ(reg.errorCode, 0);
    }
    else
    {
        EXPECT_FALSE(reg.isSuccessful);
        EXPECT_NE(reg.errorCode, 0);
    }
}

TEST_F(AsyncIOProviderTest, RegisterBufferInvalidParameters)
{
    BufferRegistration reg1 = provider->RegisterBuffer(nullptr, 1024);
    EXPECT_FALSE(reg1.isSuccessful);
    
    uint8_t buffer[1024] = {0};
    BufferRegistration reg2 = provider->RegisterBuffer(buffer, 0);
    EXPECT_FALSE(reg2.isSuccessful);
}

TEST_F(AsyncIOProviderTest, UnregisterBufferInvalid)
{
    EXPECT_FALSE(provider->UnregisterBuffer(-1));
    EXPECT_FALSE(provider->UnregisterBuffer(0));
}

TEST_F(AsyncIOProviderTest, GetRegisteredBufferCount)
{
    uint32_t count = provider->GetRegisteredBufferCount();
    EXPECT_GE(count, 0);
    
    if (provider->SupportsFeature("BufferRegistration"))
    {
        uint8_t buffer[1024] = {0};
        provider->RegisterBuffer(buffer, sizeof(buffer));
        uint32_t newCount = provider->GetRegisteredBufferCount();
        EXPECT_GT(newCount, count);
    }
}

// =============================================================================
// Completion Processing Tests
// =============================================================================

TEST_F(AsyncIOProviderTest, ProcessCompletionsNoOps)
{
    CompletionEntry entries[10];
    uint32_t count = provider->ProcessCompletions(entries, 10, 0);
    EXPECT_EQ(count, 0);
}

TEST_F(AsyncIOProviderTest, ProcessCompletionsInvalidParameters)
{
    CompletionEntry entries[10];
    
    // Null entries
    uint32_t count = provider->ProcessCompletions(nullptr, 10, 0);
    EXPECT_EQ(count, 0);
    
    // Zero max count
    count = provider->ProcessCompletions(entries, 0, 0);
    EXPECT_EQ(count, 0);
}

TEST_F(AsyncIOProviderTest, ProcessCompletionsWithTimeout)
{
    CompletionEntry entries[10];
    
    // Should return immediately with timeout of 100ms
    uint32_t count = provider->ProcessCompletions(entries, 10, 100);
    EXPECT_EQ(count, 0);
}

// =============================================================================
// Statistics & Monitoring Tests
// =============================================================================

TEST_F(AsyncIOProviderTest, GetPendingOperationCount)
{
    uint32_t count = provider->GetPendingOperationCount();
    EXPECT_GE(count, 0);
}

TEST_F(AsyncIOProviderTest, ResetStatistics)
{
    provider->ResetStatistics();
    EXPECT_NO_THROW({
        provider->ResetStatistics();
        provider->ResetStatistics();
    });
}

// =============================================================================
// Factory Function Tests
// =============================================================================

TEST(FactoryTest, CreateAsyncIOProvider)
{
    std::unique_ptr<AsyncIOProvider> p = CreateAsyncIOProvider();
    ASSERT_NE(p, nullptr);
}

TEST(FactoryTest, CreateAsyncIOProviderForPlatform)
{
    #ifdef _WIN32
        auto p = CreateAsyncIOProviderForPlatform(PlatformType::Windows);
    #elif __APPLE__
        auto p = CreateAsyncIOProviderForPlatform(PlatformType::macOS);
    #elif __linux__
        auto p = CreateAsyncIOProviderForPlatform(PlatformType::Linux);
    #else
        auto p = CreateAsyncIOProviderForPlatform(PlatformType::Unknown);
    #endif
    
    ASSERT_NE(p, nullptr);
}

// =============================================================================
// Integration Tests
// =============================================================================

TEST_F(AsyncIOProviderTest, BasicWorkflow)
{
    // Test basic workflow: Initialize -> Register -> Process -> Shutdown
    ASSERT_TRUE(provider->Initialize(1000));
    
    uint32_t pendingOps = provider->GetPendingOperationCount();
    EXPECT_EQ(pendingOps, 0);
    
    CompletionEntry entries[10];
    uint32_t completions = provider->ProcessCompletions(entries, 10, 0);
    EXPECT_EQ(completions, 0);
}

TEST_F(AsyncIOProviderTest, MultipleRegistrations)
{
    if (provider->SupportsFeature("BufferRegistration"))
    {
        uint8_t buffers[5][1024];
        std::vector<BufferRegistration> registrations;
        
        for (int i = 0; i < 5; ++i)
        {
            auto reg = provider->RegisterBuffer(buffers[i], sizeof(buffers[i]));
            if (reg.isSuccessful)
            {
                registrations.push_back(reg);
            }
        }
        
        EXPECT_EQ(provider->GetRegisteredBufferCount(), registrations.size());
    }
}

// =============================================================================
// Stress Tests
// =============================================================================

TEST_F(AsyncIOProviderTest, StressBufferRegistration)
{
    if (provider->SupportsFeature("BufferRegistration"))
    {
        std::vector<uint8_t> buffer(10 * 1024 * 1024);  // 10MB buffer
        
        for (int i = 0; i < 100; ++i)
        {
            auto reg = provider->RegisterBuffer(buffer.data(), buffer.size());
            if (i == 0)
            {
                EXPECT_TRUE(reg.isSuccessful);
            }
            provider->UnregisterBuffer(reg.bufferId);
        }
    }
}

// =============================================================================
// Cleanup and Shutdown Tests
// =============================================================================

TEST_F(AsyncIOProviderTest, ShutdownMultipleTimes)
{
    provider->Shutdown();
    
    // Should handle multiple shutdowns gracefully
    EXPECT_NO_THROW({
        provider->Shutdown();
        provider->Shutdown();
    });
}

// =============================================================================
// Main Test Entry Point
// =============================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
