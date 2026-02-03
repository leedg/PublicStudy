#pragma once

// English: GameSession - extended session for game logic
// 한글: GameSession - 게임 로직 확장 세션

#include "Network/Core/PacketDefine.h"
#include "Network/Core/Session.h"
#include "TestClientPacketHandler.h"
#include "Utils/NetworkUtils.h"
#include <memory>

namespace Network::TestServer
{
class GameSession : public Core::Session
{
  public:
	GameSession();
	~GameSession() override;

	// English: Session event overrides
	// 한글: 세션 이벤트 오버라이드
	void OnConnected() override;
	void OnDisconnected() override;
	void OnRecv(const char *data, uint32_t size) override;

	// English: DB connect time recording
	// 한글: DB 접속 시간 기록
	void RecordConnectTimeToDB();

	bool IsConnectionRecorded() const { return mConnectionRecorded; }

  private:
	void ProcessPacket(const Core::PacketHeader *header, const char *data,
					   uint32_t size);

  private:
	TestClientPacketHandler mClientPacketHandler;
	bool mConnectionRecorded;
};

using GameSessionRef = std::shared_ptr<GameSession>;

} // namespace Network::TestServer
