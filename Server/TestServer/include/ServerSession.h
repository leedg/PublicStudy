#pragma once

// English: ServerSession - common base for server-to-server communication sessions
// 한글: ServerSession - 서버간 통신 공통 베이스 세션 (암호화 없음)

#include "Network/Core/Session.h"
#include <cstdint>
#include <functional>

namespace Network::TestServer
{
    // =============================================================================
    // English: ServerSession - intermediate base for inter-server sessions
    //          No encryption. Holds ping sequence and connection timestamp.
    //          Not abstract — can be used directly for simple server connections.
    // 한글: ServerSession - 서버간 세션 중간 추상 클래스
    //       암호화 없음. Ping sequence, 연결 시간 보유.
    //       순수 가상 없음 — 단순 서버 연결에 직접 사용 가능.
    // =============================================================================

    class ServerSession : public Core::Session
    {
    public:
        ServerSession();
        virtual ~ServerSession();

        // English: Reconnect callback interface — called by owner (e.g. TestServer) on disconnect
        // 한글: 재연결 콜백 인터페이스 — 연결 끊김 시 소유자(TestServer 등)가 호출
        using ReconnectCallback = std::function<void()>;
        void SetReconnectCallback(ReconnectCallback cb) { mReconnectCallback = std::move(cb); }

        // English: Session event overrides (no-op defaults; subclasses override as needed)
        // 한글: 세션 이벤트 오버라이드 (기본 no-op; 서브클래스에서 필요 시 오버라이드)
        void OnConnected() override;
        void OnDisconnected() override;
        void OnRecv(const char* data, uint32_t size) override;

    protected:
        ReconnectCallback mReconnectCallback;
    };

    using ServerSessionRef = std::shared_ptr<ServerSession>;

} // namespace Network::TestServer
