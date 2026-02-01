#include "../include/TestMessageHandler.h"
#include <chrono>

namespace TestServer {

TestServerMessageHandler::TestServerMessageHandler() {
}

void TestServerMessageHandler::InitializeMessageHandlers() {
    // Register Ping message handler
    RegisterHandler(Network::Interfaces::MessageType::Ping,
        [this](const Network::Interfaces::Message& msg) { OnPingMessageReceived(msg); });

    // Register Pong message handler
    RegisterHandler(Network::Interfaces::MessageType::Pong,
        [this](const Network::Interfaces::Message& msg) { OnPongMessageReceived(msg); });

    // Register custom message handler for application-specific messages
    RegisterHandler(static_cast<Network::Interfaces::MessageType>(
        static_cast<uint32_t>(Network::Interfaces::MessageType::CustomStart)),
        [this](const Network::Interfaces::Message& msg) { OnCustomMessageReceived(msg); });

    std::cout << "[TestServerMessageHandler] Message handlers initialized successfully" << std::endl;
}

void TestServerMessageHandler::OnPingMessageReceived(const Network::Interfaces::Message& message) {
    std::cout << "[TestServerMessageHandler] PING received from connection "
              << message.connectionId << std::endl;

    // Create and prepare PONG response message
    // In a full implementation, this would be sent via the network engine
    auto pongResponseMessage = CreateMessage(
        Network::Interfaces::MessageType::Pong,
        message.connectionId,
        nullptr,
        0
    );

    std::cout << "[TestServerMessageHandler] PONG response prepared for connection "
              << message.connectionId << std::endl;
}

void TestServerMessageHandler::OnPongMessageReceived(const Network::Interfaces::Message& message) {
    // Calculate round-trip latency
    auto currentTimestamp = GetCurrentTimestamp();
    uint64_t roundTripLatencyMs = currentTimestamp - message.timestamp;

    std::cout << "[TestServerMessageHandler] PONG received from connection "
              << message.connectionId
              << " (round-trip latency: " << roundTripLatencyMs << "ms)" << std::endl;
}

void TestServerMessageHandler::OnCustomMessageReceived(const Network::Interfaces::Message& message) {
    std::cout << "[TestServerMessageHandler] Custom message received from connection "
              << message.connectionId
              << " (payload size: " << message.data.size() << " bytes)" << std::endl;

    // TODO: Implement application-specific message processing logic here
    // This is where TestServer's business logic for custom messages would be implemented
    // Examples: game state updates, chat messages, player actions, etc.
}

} // namespace TestServer
