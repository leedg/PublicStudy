// UTF-8 인코딩 워닝 억제
#pragma warning(disable: 4566)

#include "Session.h"
#include "RIONetwork.h"
#include "RIOWorker.h"
#include "BufferPool.h"
#include "Config.h"
#include <cstdio>

// ============================================================
// 세션 생성자
// ============================================================
Session::Session(SOCKET s, RIOWorker* worker, BufferPool* pool)
    : m_sock(s), m_worker(worker), m_pool(pool) 
{
    // *** Breakpoint 10: Session constructor ***
    printf("[DEBUG] Session constructor: socket=%llu\n", (unsigned long long)s);
}

Session::~Session() 
{ 
    stop(); 
}

// ============================================================
// 세션 시작 - RIO Request Queue 생성 및 Recv 등록
// ============================================================
bool Session::start() 
{
    // *** Breakpoint 11: Session start ***
    printf("[DEBUG] Session::start: Creating RIORequestQueue (socket=%llu)\n", 
           (unsigned long long)m_sock);
    
    m_alive.store(true);
    
    // RIO Request Queue 생성
    m_rq = RIONetwork::Rio().RIOCreateRequestQueue(
        m_sock,
        Config::RecvOutstandingPerSession, 1,
        Config::SendOutstandingPerSession, 1,
        m_worker->cq(), m_worker->cq(), this);
    
    if (m_rq == RIO_INVALID_RQ) 
    {
        printf("[ERROR] RIOCreateRequestQueue failed (socket=%llu)\n", (unsigned long long)m_sock);
        
        return false;
    }

    // *** Breakpoint 12: RIO Request Queue created ***
    printf("[DEBUG] Session::start: RIORequestQueue created (socket=%llu, rq=%llu)\n", 
           (unsigned long long)m_sock, (unsigned long long)m_rq);

    // TCP_NODELAY 옵션 설정 (Nagle 알고리즘 비활성화)
    if (Config::TcpNoDelay) 
    {
        DWORD on = 1;
        setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));
    }

    // *** Breakpoint 13: Posting initial Recv requests ***
    printf("[DEBUG] Session::start: Posting %d Recv requests (socket=%llu)\n", 
           Config::RecvOutstandingPerSession, (unsigned long long)m_sock);
    
    // 초기 Recv 요청 등록
    for (int i = 0; i < Config::RecvOutstandingPerSession; ++i) 
    {
        if (!postRecv())
        {
            break;
        }
    }
    
    // *** Breakpoint 14: Session started ***
    printf("[DEBUG] Session::start: Session started (socket=%llu)\n\n", (unsigned long long)m_sock);
    
    return true;
}

// ============================================================
// 세션 중지
// ============================================================
void Session::stop() 
{
    bool expected = true;
    
    if (!m_alive.compare_exchange_strong(expected, false))
    {
        return;
    }
    
    // 소켓 닫기
    if (m_sock != INVALID_SOCKET) 
    {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    
    m_rq = RIO_INVALID_RQ;
}

// ============================================================
// Recv 요청 등록
// ============================================================
bool Session::postRecv() 
{
    if (!m_alive.load())
    {
        return false;
    }
    
    // 버퍼 슬라이스 할당
    uint32_t off = 0;
    
    if (!m_pool->allocSlice(off))
    {
        return false;
    }

    // ExtRioBuf 생성 및 설정
    auto* eb = new ExtRioBuf();
    eb->op = OpType::Recv;
    eb->owner = this;
    eb->BufferId = m_pool->bufferId();
    eb->Offset = off;
    eb->Length = Config::SliceSize;
    eb->sliceOffset = off;

    // RIO Receive 요청 등록
    if (!RIONetwork::Rio().RIOReceive(m_rq, eb, 1, 0, eb)) 
    {
        m_pool->freeSlice(off);
        delete eb;
        
        return false;
    }
    
    m_inFlightRecv.fetch_add(1, std::memory_order_relaxed);
    
    return true;
}

// ============================================================
// Send 요청 등록
// ============================================================
bool Session::postSend(uint32_t offset, uint32_t len) 
{
    if (!m_alive.load())
    {
        return false;
    }

    // ExtRioBuf 생성 및 설정
    auto* eb = new ExtRioBuf();
    eb->op = OpType::Send;
    eb->owner = this;
    eb->BufferId = m_pool->bufferId();
    eb->Offset = offset;
    eb->Length = len;
    eb->sliceOffset = offset;

    // RIO Send 요청 등록
    if (!RIONetwork::Rio().RIOSend(m_rq, eb, 1, 0, eb)) 
    {
        m_pool->freeSlice(offset);
        delete eb;
        
        return false;
    }
    
    m_inFlightSend.fetch_add(1, std::memory_order_relaxed);
    
    return true;
}

// ============================================================
// Recv 완료 콜백 - 에코를 위해 받은 데이터를 그대로 Send
// ============================================================
void Session::onRecvComplete(ExtRioBuf* eb, DWORD bytes, DWORD status) 
{
    m_inFlightRecv.fetch_sub(1, std::memory_order_relaxed);
    
    // 에러 또는 연결 종료 체크
    if (!m_alive.load() || status != NO_ERROR || bytes == 0) 
    {
        m_pool->freeSlice(eb->sliceOffset);
        delete eb;
        stop();
        
        return;
    }
    
    // 에코: 받은 데이터를 그대로 전송
    if (!postSend(eb->sliceOffset, bytes)) 
    {
        m_pool->freeSlice(eb->sliceOffset);
    }
    
    delete eb;
}

// ============================================================
// Send 완료 콜백 - 전송 완료 후 다시 Recv 등록
// ============================================================
void Session::onSendComplete(ExtRioBuf* eb, DWORD /*bytes*/, DWORD status) 
{
    m_inFlightSend.fetch_sub(1, std::memory_order_relaxed);
    
    // 버퍼 해제
    m_pool->freeSlice(eb->sliceOffset);
    delete eb;
    
    // 에러 체크
    if (!m_alive.load() || status != NO_ERROR) 
    {
        stop();
        
        return;
    }
    
    // 다음 데이터 수신을 위해 Recv 재등록
    postRecv();
}
