#pragma once

// English: DBServerSession - session for DB server connection
// 한글: DBServerSession - DB 서버 연결 전용 세션

#include "ServerSession.h"
#include "DBServerPacketHandler.h"
#include <memory>

namespace Network::TestServer
{
    // =============================================================================
    // English: DBServerSession - inherits ServerSession, owns DBServerPacketHandler
    //          OnRecv() delegates to DBServerPacketHandler::ProcessPacket()
    //          Connection lifecycle management is handled by TestServer.
    // 한글: DBServerSession - ServerSession 상속, DBServerPacketHandler 소유
    //       OnRecv()에서 DBServerPacketHandler::ProcessPacket() 호출
    //       연결 관리는 TestServer가 담당.
    // =============================================================================

    class DBServerSession : public ServerSession
    {
    public:
        DBServerSession();
        virtual ~DBServerSession();

        // English: Session event overrides
        // 한글: 세션 이벤트 오버라이드
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

        // English: Access the packet handler (for sending pings etc.)
        // 한글: 패킷 핸들러 접근 (핑 전송 등)
        DBServerPacketHandler* GetPacketHandler() { return mPacketHandler.get(); }

    private:
        std::unique_ptr<DBServerPacketHandler> mPacketHandler;
    };

    using DBServerSessionRef = std::shared_ptr<DBServerSession>;

} // namespace Network::TestServer
