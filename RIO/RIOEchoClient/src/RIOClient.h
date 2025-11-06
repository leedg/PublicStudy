#pragma once

#include "RIONetwork.h"
#include "echo_message.pb.h"

#include <string>

class RIOClient {
public:
    RIOClient();
    ~RIOClient();

    bool connectTo(const std::string& host, uint16_t port);
    bool sendEcho(const rio::echo::EchoMessage& message);
    bool receiveEcho(rio::echo::EchoMessage& message, DWORD timeoutMillis);

private:
    struct RioContext {
        enum class Op { Send, Recv } op;
    };

    bool ensureBuffers();
    void cleanup();

private:
    SOCKET      m_socket = INVALID_SOCKET;
    RIO_RQ      m_rq = RIO_INVALID_RQ;
    RIO_CQ      m_cq = RIO_INVALID_CQ;
    HANDLE      m_event = NULL;
    RIO_BUFFERID m_bufferId = RIO_INVALID_BUFFERID;
    char*       m_buffer = nullptr;
    static constexpr uint32_t kBufferSize = 4096;
    static constexpr uint32_t kSendOffset = 0;
    static constexpr uint32_t kRecvOffset = 2048;

    RioContext  m_sendCtx{ RioContext::Op::Send };
    RioContext  m_recvCtx{ RioContext::Op::Recv };
    bool        m_sendPending = false;
    bool        m_recvPending = false;
};
