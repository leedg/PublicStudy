#pragma once

// English: DB server message handler for TestServer
// 한글: TestServer의 DB 서버 메시지 핸들러

#include "Tests/Protocols/MessageHandler.h"
#include "Tests/Protocols/PingPong.h"
#include "Utils/NetworkUtils.h"
#include <functional>
#include <string>
#include <vector>

namespace Network::TestServer
{
class TestDbServerMessageHandler
{
  public:
	using SendCallback =
		std::function<void(Network::Protocols::ConnectionId,
						   const std::vector<uint8_t> &)>;

	TestDbServerMessageHandler();

	// English: Initialize ping/pong handlers
	// 한글: Ping/Pong 핸들러 초기화
	void Initialize();

	// English: Register callback used by transport layer
	// 한글: 전송 계층에서 사용하는 콜백 등록
	void SetSendCallback(SendCallback callback);

	// English: Process raw data from DB server
	// 한글: DB 서버로부터 받은 원시 데이터를 처리
	void ProcessIncomingData(Network::Protocols::ConnectionId connectionId,
							 const uint8_t *data, size_t size);

	// English: Send ping to DB server
	// 한글: DB 서버로 Ping 전송
	void SendPing(Network::Protocols::ConnectionId connectionId,
				  const std::string &message = "ping");

  private:
	void OnPingMessageReceived(const Network::Protocols::Message &message);
	void OnPongMessageReceived(const Network::Protocols::Message &message);

	void SendMessage(Network::Protocols::MessageType type,
					 Network::Protocols::ConnectionId connectionId,
					 const std::vector<uint8_t> &payload);

  private:
	SendCallback mSendCallback;
	Network::Protocols::MessageHandler mMessageHandler;
	Network::Protocols::PingPongHandler mPingPongHandler;
};

} // namespace Network::TestServer
