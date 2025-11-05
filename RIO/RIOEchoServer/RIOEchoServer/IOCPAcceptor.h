#pragma once
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include "Config.h"
#include "RIOWorker.h"

class IOCPAcceptor {
public:
    IOCPAcceptor(uint16_t port, std::vector<std::unique_ptr<RIOWorker>>& workers);
    ~IOCPAcceptor();

    bool start();
    void stop();
    bool running() const { return m_run.load(); }

private:
    struct AcceptOv : public OVERLAPPED {
        SOCKET acceptSock = INVALID_SOCKET;
        char addrBuf[(sizeof(sockaddr_in) + 16) * 2];
    };

    void threadProc();
    void postOneAccept(AcceptOv* aov);
    void loadAcceptEx(SOCKET s);

private:
    uint16_t m_port = 0;
    SOCKET   m_listen = INVALID_SOCKET;
    HANDLE   m_iocp = NULL;
    std::thread m_thr;
    std::atomic<bool> m_run{ false };

    LPFN_ACCEPTEX pAcceptEx = nullptr;
    LPFN_GETACCEPTEXSOCKADDRS pGetSockaddrs = nullptr;

    std::vector<AcceptOv*> m_pool;

    std::vector<std::unique_ptr<RIOWorker>>* m_workers = nullptr;
    std::atomic<uint32_t> m_rr{ 0 };
};
