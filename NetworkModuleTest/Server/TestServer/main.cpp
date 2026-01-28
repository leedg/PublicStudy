// English: Simple test program for NetworkModuleTest
// 한글: NetworkModuleTest용 간단한 테스트 프로그램

#include "../Server/ServerEngine/Core/Types.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace Network::AsyncIO;
using namespace Network::Protocols;

int main()
{
    std::cout << "====================================" << std::endl;
    std::cout << "NetworkModuleTest Simple Test" << std::endl;
    std::cout << "====================================" << std::endl;
    
    try
    {
        // Test 1: Platform Detection
        std::cout << "\n=== Platform Detection Test ===" << std::endl;
        
        auto provider = CreateAsyncIOProvider();
        if (provider)
        {
            std::cout << "[PASS] AsyncIO provider created successfully" << std::endl;
            
            const auto& info = provider->GetInfo();
            std::cout << "Backend: " << info.mName << std::endl;
            std::cout << "Platform Type: " << static_cast<int>(info.mPlatformType) << std::endl;
            std::cout << "Buffer Registration: " << (info.mSupportsBufferReg ? "Yes" : "No") << std::endl;
            std::cout << "Batching: " << (info.mSupportsBatching ? "Yes" : "No") << std::endl;
            std::cout << "Zero-copy: " << (info.mSupportsZeroCopy ? "Yes" : "No") << std::endl;
            
            // Test 2: Initialization
            std::cout << "\n=== Initialization Test ===" << std::endl;
            auto error = provider->Initialize(256, 1000);
            if (error == AsyncIOError::Success)
            {
                std::cout << "[PASS] Provider initialized successfully" << std::endl;
                
                if (provider->IsInitialized())
                {
                    std::cout << "[PASS] IsInitialized returns true" << std::endl;
                }
                
                // Test 3: Statistics
                std::cout << "\n=== Statistics Test ===" << std::endl;
                auto stats = provider->GetStats();
                std::cout << "Total Requests: " << stats.mTotalRequests << std::endl;
                std::cout << "Total Completions: " << stats.mTotalCompletions << std::endl;
                std::cout << "Pending Requests: " << stats.mPendingRequests << std::endl;
                
                provider->Shutdown();
                std::cout << "[PASS] Provider shutdown successfully" << std::endl;
            }
            else
            {
                std::cout << "[FAIL] Provider initialization failed: " 
                          << static_cast<int>(error) << std::endl;
            }
        }
        else
        {
            std::cout << "[FAIL] Failed to create AsyncIO provider" << std::endl;
        }
        
        // Test 4: Message Handler
        std::cout << "\n=== Message Handler Test ===" << std::endl;
        auto messageHandler = std::make_unique<MessageHandler>();
        std::cout << "[PASS] MessageHandler created successfully" << std::endl;
        
        // Register ping handler
        bool pingReceived = false;
        messageHandler->RegisterHandler(
            MessageType::Ping,
            [&pingReceived](const Message& msg) {
                pingReceived = true;
                std::cout << "Ping message received from connection: " 
                          << msg.mConnectionId << std::endl;
            }
        );
        std::cout << "[PASS] Ping handler registered" << std::endl;
        
        // Create a test message
        std::string testData = "Hello, Network!";
        auto messageData = messageHandler->CreateMessage(
            MessageType::Ping, 
            12345, // connection ID
            testData.c_str(), 
            testData.length()
        );
        
        if (!messageData.empty())
        {
            std::cout << "[PASS] Message created successfully" << std::endl;
            
            // Process the message
            if (messageHandler->ProcessMessage(12345, messageData.data(), messageData.size()))
            {
                std::cout << "[PASS] Message processed successfully" << std::endl;
                
                if (pingReceived)
                {
                    std::cout << "[PASS] Ping handler was called" << std::endl;
                }
                else
                {
                    std::cout << "[FAIL] Ping handler was not called" << std::endl;
                }
            }
            else
            {
                std::cout << "[FAIL] Message processing failed" << std::endl;
            }
        }
        else
        {
            std::cout << "[FAIL] Message creation failed" << std::endl;
        }
        
        std::cout << "\n====================================" << std::endl;
        std::cout << "All tests completed" << std::endl;
        std::cout << "====================================" << std::endl;
        
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Test exception: " << e.what() << std::endl;
        return 1;
    }
}