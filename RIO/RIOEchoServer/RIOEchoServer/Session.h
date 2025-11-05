#pragma once
#include <winsock2.h>
#include <MSWSock.h>
#include <atomic>
#include <cstdint>

class RIOWorker;
class BufferPool;

enum class OpType : int { Recv=1, Send=2 };

struct ExtRioBuf : public RIO_BUF {
    OpType op;
    class Session* owner;
    uint32_t sliceOffset;
};

class Session {
public:
    Session(SOCKET s, RIOWorker* worker, BufferPool* pool);
    ~Session();

    bool   start();
    void   stop();
    bool   alive() const { return m_alive.load(std::memory_order_relaxed); }

    void   onRecvComplete(ExtRioBuf* eb, DWORD bytes, DWORD status);
    void   onSendComplete(ExtRioBuf* eb, DWORD bytes, DWORD status);

    bool   postRecv();
    bool   postSend(uint32_t offset, uint32_t len);

    RIO_RQ rq() const { return m_rq; }
    SOCKET socket() const { return m_sock; }

private:
    SOCKET      m_sock = INVALID_SOCKET;
    RIO_RQ      m_rq   = RIO_INVALID_RQ;
    RIOWorker*  m_worker = nullptr;
    BufferPool* m_pool = nullptr;

    std::atomic<bool> m_alive{ false };
    std::atomic<int>  m_inFlightRecv{ 0 };
    std::atomic<int>  m_inFlightSend{ 0 };
};
