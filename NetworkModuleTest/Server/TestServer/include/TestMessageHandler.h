#pragma once

#include "../../ServerEngine/Implementations/Protocols/BaseMessageHandler.h"
#include <iostream>

namespace TestServer
{

/**
 * TestServer specific message handler implementation
 * Implements business logic for handling network messages
 *
 * Naming Convention:
 * - TestServer prefix indicates this is server-specific implementation
 * - Handler suffix indicates message processing responsibility
 */
class TestServerMessageHandler
	: public Network::Implementations::BaseMessageHandler
{
  public:
	TestServerMessageHandler();
	virtual ~TestServerMessageHandler() = default;

	/**
	 * Initialize handler with server-specific message processors
	 * Sets up all message type handlers (Ping, Pong, Custom)
	 */
	void InitializeMessageHandlers();

  private:
	/**
	 * Handle incoming Ping message
	 * Sends Pong response back to client
	 */
	void OnPingMessageReceived(const Network::Interfaces::Message &message);

	/**
	 * Handle incoming Pong message
	 * Calculates and logs round-trip latency
	 */
	void OnPongMessageReceived(const Network::Interfaces::Message &message);

	/**
	 * Handle custom application-specific messages
	 * Process game/server-specific message types
	 */
	void OnCustomMessageReceived(const Network::Interfaces::Message &message);
};

} // namespace TestServer
