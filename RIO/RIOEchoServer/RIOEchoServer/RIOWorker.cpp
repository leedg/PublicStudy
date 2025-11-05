// UTF-8 인코딩 워닝 억제
#pragma warning(disable: 4566)

#include "RIOWorker.h"
#include "Config.h"
#include <cstdio>

static inline void cpu_relax() 
{ 
    YieldProcessor(); 
}

RIOWorker::RIOWorker(int index) : m_index(index) {}

RIOWorker::~RIOWorker() 
{ 
    stop(); 
}

// ============================================================
// 워커 시작
// ============================================================
bool RIOWorker::start() 
{
    // Completion Queue 생성
    m_cq = RIONetwork::CreateCQ(Config::CQSizePerWorker, m_event);
    
    if (m_cq == RIO_INVALID_CQ)
    {
        return false;
    }

    // 버퍼 풀 초기화
    if (!m_pool.init(Config::PoolBytes, Config::SliceSize)) 
    {
        printf("[FATAL] Worker %d buffer pool init failed\n", m_index);
        
        return false;
    }

    // 워커 스레드 시작
    m_run.store(true);
    m_thr = std::thread(&RIOWorker::run, this);
    
    return true;
}

// ============================================================
// 워커 중지
// ============================================================
void RIOWorker::stop() 
{
    bool expected = true;
    
    if (!m_run.compare_exchange_strong(expected, false))
    {
        return;
    }
    
    if (m_thr.joinable())
    {
        m_thr.join();
    }

    // 모든 세션 정리
    m_sessions.clear();

    // Completion Queue 정리
    if (m_cq != RIO_INVALID_CQ) 
    {
        RIONetwork::Rio().RIOCloseCompletionQueue(m_cq);
        m_cq = RIO_INVALID_CQ;
    }
    
    // 이벤트 핸들 정리
    if (m_event) 
    { 
        CloseHandle(m_event);
        m_event = NULL;
    }
    
    // 버퍼 풀 정리
    m_pool.cleanup();
}

// ============================================================
// Accept된 소켓을 큐에 추가
// ============================================================
void RIOWorker::enqueueAccepted(SOCKET s) 
{
    // *** Breakpoint 6: Worker enqueuing socket ***
    printf("[DEBUG] Worker[%d] enqueueAccepted: Enqueuing socket (socket=%llu)\n", 
           m_index, (unsigned long long)s);
    
    std::lock_guard<std::mutex> lk(m_acceptLock);
    m_acceptQ.push(s);
    
    printf("[DEBUG] Worker[%d] enqueueAccepted: Queue size=%zu\n", m_index, m_acceptQ.size());
}

// ============================================================
// Accept 큐에서 소켓을 꺼내 세션 생성
// ============================================================
void RIOWorker::drainAccepted() 
{
    for (;;) 
    {
        SOCKET s = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lk(m_acceptLock);
            
            if (m_acceptQ.empty())
            {
                break;
            }
            
            s = m_acceptQ.front();
            m_acceptQ.pop();
            
            // *** Breakpoint 7: Dequeued socket ***
            printf("[DEBUG] Worker[%d] drainAccepted: Dequeued socket (socket=%llu, remaining=%zu)\n", 
                   m_index, (unsigned long long)s, m_acceptQ.size());
        }
        
        // *** Breakpoint 8: Creating session ***
        printf("[DEBUG] Worker[%d] drainAccepted: Creating session (socket=%llu)\n", 
               m_index, (unsigned long long)s);
        
        auto sess = std::make_unique<Session>(s, this, &m_pool);
        
        if (!sess->start()) 
        {
            printf("[ERROR] Worker[%d] drainAccepted: Session start failed (socket=%llu)\n", 
                   m_index, (unsigned long long)s);
            closesocket(s);
            
            continue;
        }
        
        // *** Breakpoint 9: Session created ***
        printf("[DEBUG] Worker[%d] drainAccepted: Session created (socket=%llu, total=%zu)\n", 
               m_index, (unsigned long long)s, m_sessions.size() + 1);
        
        m_sessions.emplace_back(std::move(sess));
    }
}

// ============================================================
// RIO Completion Queue에서 완료된 I/O 처리
// ============================================================
void RIOWorker::handleCompletions() 
{
    RIORESULT results[512];
    ULONG n = RIONetwork::Rio().RIODequeueCompletion(m_cq, results, 512);
    
    if (n == 0)
    {
        return;
    }
    
    if (n == RIO_CORRUPT_CQ) 
    {
        printf("[FATAL] RIO_CORRUPT_CQ on worker %d\n", m_index);
        
        return;
    }
    
    // 완료된 각 I/O 작업 처리
    for (ULONG i = 0; i < n; ++i) 
    {
        auto& r = results[i];
        auto* eb = reinterpret_cast<ExtRioBuf*>(r.RequestContext);
        Session* s = eb->owner;
        
        if (!s)
        {
            continue;
        }
        
        // Recv/Send 완료 콜백 호출
        if (eb->op == OpType::Recv)
        {
            s->onRecvComplete(eb, r.BytesTransferred, r.Status);
        }
        else
        {
            s->onSendComplete(eb, r.BytesTransferred, r.Status);
        }
    }
}

// ============================================================
// 워커 메인 루프
// ============================================================
void RIOWorker::run() 
{
    printf("[WORKER %d] Started (polling=%s)\n", m_index, Config::UsePolling ? "enabled" : "disabled");
    
    int spin = 0;
    
    while (m_run.load(std::memory_order_relaxed)) 
    {
        // Accept된 소켓 처리
        drainAccepted();
        
        // I/O 완료 처리
        handleCompletions();

        // 대기 전략
        if (Config::UsePolling) 
        {
            if (++spin >= Config::PollBusySpinIters) 
            {
                spin = 0;
                
                if (Config::PollSleepMicros > 0) 
                {
                    Sleep(0);
                } 
                else 
                {
                    cpu_relax();
                }
            } 
            else 
            {
                cpu_relax();
            }
        } 
        else 
        {
            Sleep(0);
        }
    }
    
    printf("[WORKER %d] Stopped\n", m_index);
}
