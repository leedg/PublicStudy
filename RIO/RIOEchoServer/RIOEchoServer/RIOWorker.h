#pragma once
#include <windows.h>
#include <vector>
#include <atomic>
#include <thread>
#include <memory>
#include <queue>
#include <mutex>
#include "RIONetwork.h"
#include "BufferPool.h"
#include "Session.h"

class RIOWorker {
public:
    explicit RIOWorker(int index);
    ~RIOWorker();

    bool   start();
    void   stop();

    void   enqueueAccepted(SOCKET s);

    RIO_CQ cq() const { return m_cq; }
    BufferPool& pool() { return m_pool; }

private:
    void   run();
    void   drainAccepted();
    void   handleCompletions();

private:
    int      m_index = 0;
    RIO_CQ   m_cq = RIO_INVALID_CQ;
    HANDLE   m_event = NULL;
    std::thread m_thr;
    std::atomic<bool> m_run{ false };

    std::mutex m_acceptLock;
    std::queue<SOCKET> m_acceptQ;

    std::vector<std::unique_ptr<Session>> m_sessions;

    BufferPool m_pool;
};
