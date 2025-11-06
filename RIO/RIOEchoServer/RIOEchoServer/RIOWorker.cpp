// UTF-8 인코딩 워닝 억제
#pragma warning(disable: 4566)

#include "RIOWorker.h"
#include "Config.h"
#include <cstdio>

// ============================================================
// CPU 최적화 함수 - Busy-Wait 시 CPU 부하 감소
// 
// YieldProcessor(): Intel의 PAUSE 명령어 또는 ARM의 YIELD
// - 하이퍼스레딩 환경에서 다른 논리 코어에 실행 기회 제공
// - 전력 소비 감소 및 스핀락 성능 향상
// ============================================================
static inline void cpu_relax() 
{ 
    YieldProcessor(); 
}

// ============================================================
// RIOWorker 생성자
// 
// 각 워커는 독립적인 스레드에서 실행되며 다음을 담당합니다:
// - Accept된 소켓으로부터 세션 생성
// - 해당 세션들의 I/O 완료 처리
// - 전용 버퍼 풀 관리
// 
// 매개변수:
//   - index: 워커 인덱스 (0부터 시작, 디버깅/로깅용)
// ============================================================
RIOWorker::RIOWorker(int index) : m_index(index) {}

// ============================================================
// RIOWorker 소멸자
// ============================================================
RIOWorker::~RIOWorker() 
{ 
    stop(); 
}

// ============================================================
// 워커 시작
// 
// 워커 스레드를 초기화하고 시작합니다.
// 
// 초기화 단계:
// 1. RIO Completion Queue 생성 (I/O 완료 통지 수신용)
// 2. 버퍼 풀 초기화 (이 워커가 관리하는 모든 세션이 공유)
// 3. 워커 스레드 시작 (run() 함수 실행)
// 
// 반환값: 성공 시 true, 실패 시 false
// ============================================================
bool RIOWorker::start() 
{
    // Completion Queue 생성
    // - 이 워커가 관리하는 모든 세션의 I/O 완료 통지가 여기로 옴
    // - 폴링 모드: 이벤트 핸들 생성 (WaitForSingleObject 사용)
    // - IOCP 모드: IOCP 핸들 사용 (GetQueuedCompletionStatus 사용)
    m_cq = RIONetwork::CreateCQ(Config::CQSizePerWorker, m_event);
    
    if (m_cq == RIO_INVALID_CQ)
    {
        return false;
    }

    // 버퍼 풀 초기화
    // - 이 워커의 모든 세션이 공유하는 대형 버퍼
    // - VirtualAlloc으로 커밋된 메모리 영역
    // - RIORegisterBuffer로 RIO API에 등록
    if (!m_pool.init(Config::PoolBytes, Config::SliceSize)) 
    {
        printf("[FATAL] Worker %d buffer pool init failed\n", m_index);
        
        return false;
    }

    // 워커 스레드 시작
    // - m_run 플래그를 true로 설정 (스레드 실행 조건)
    // - std::thread로 run() 함수를 별도 스레드에서 실행
    m_run.store(true);
    m_thr = std::thread(&RIOWorker::run, this);
    
    return true;
}

// ============================================================
// 워커 중지
// 
// 워커 스레드를 안전하게 종료하고 모든 리소스를 정리합니다.
// 
// 정리 순서:
// 1. m_run 플래그를 false로 설정 (스레드 종료 신호)
// 2. 스레드 종료 대기 (join)
// 3. 모든 세션 정리 (소켓 닫기, 메모리 해제)
// 4. Completion Queue 닫기
// 5. 이벤트 핸들 닫기 (폴링 모드인 경우)
// 6. 버퍼 풀 정리 (메모리 해제)
// ============================================================
void RIOWorker::stop() 
{
    bool expected = true;
    
    // Compare-And-Swap으로 중복 호출 방지
    // (이미 false면 다른 스레드가 stop을 호출 중)
    if (!m_run.compare_exchange_strong(expected, false))
    {
        return;
    }
    
    // 워커 스레드 종료 대기
    if (m_thr.joinable())
    {
        m_thr.join();  // run() 함수가 종료될 때까지 대기
    }

    // 모든 세션 정리
    // - unique_ptr이므로 자동으로 Session::~Session() 호출
    // - 소켓 닫기 및 RIO Request Queue 정리
    m_sessions.clear();

    // Completion Queue 정리
    if (m_cq != RIO_INVALID_CQ) 
    {
        RIONetwork::Rio().RIOCloseCompletionQueue(m_cq);
        m_cq = RIO_INVALID_CQ;
    }
    
    // 이벤트 핸들 정리 (폴링 모드인 경우에만 생성됨)
    if (m_event) 
    { 
        CloseHandle(m_event);
        m_event = NULL;
    }
    
    // 버퍼 풀 정리
    // - RIODeregisterBuffer 호출
    // - VirtualFree로 메모리 해제
    m_pool.cleanup();
}

// ============================================================
// Accept된 소켓을 큐에 추가
// 
// IOCPAcceptor가 클라이언트 연결을 수락하면 이 함수를 호출하여
// 소켓을 워커의 큐에 추가합니다. (스레드 간 통신)
// 
// 동작:
// 1. 뮤텍스 락 획득 (스레드 안전성)
// 2. 소켓을 큐에 추가
// 3. 뮤텍스 락 해제
// 
// 참고: 워커 스레드(run)가 주기적으로 이 큐를 확인하여
//       새로운 소켓으로부터 세션을 생성합니다.
// 
// 매개변수:
//   - s: Accept된 소켓 핸들 (WSA_FLAG_REGISTERED_IO로 생성됨)
// ============================================================
void RIOWorker::enqueueAccepted(SOCKET s) 
{
    printf("[DEBUG] Worker[%d] enqueueAccepted: Enqueuing socket (socket=%llu)\n", 
           m_index, (unsigned long long)s);
    
    // RAII 패턴으로 자동 락 해제 보장
    std::lock_guard<std::mutex> lk(m_acceptLock);
    m_acceptQ.push(s);
    
    printf("[DEBUG] Worker[%d] enqueueAccepted: Queue size=%zu\n", m_index, m_acceptQ.size());
}

// ============================================================
// Accept 큐에서 소켓을 꺼내 세션 생성
// 
// 큐에 쌓인 모든 소켓을 처리하여 Session 객체를 생성합니다.
// 
// 동작 과정:
// 1. 큐에서 소켓 하나 꺼내기 (빈 큐이면 종료)
// 2. Session 객체 생성 (소켓, 워커, 버퍼풀 전달)
// 3. Session::start() 호출 (RIO Request Queue 생성 등)
// 4. 세션을 워커의 세션 리스트에 추가
// 5. 큐가 빌 때까지 반복
// 
// 참고: 이 함수는 워커 스레드(run)에서 호출되므로
//       세션 생성은 워커 스레드에서 이루어집니다.
// ============================================================
void RIOWorker::drainAccepted() 
{
    // 무한 루프: 큐가 빌 때까지 계속 처리
    for (;;) 
    {
        SOCKET s = INVALID_SOCKET;
        {
            // 짧은 락 구간: 큐에서 소켓 하나만 꺼내기
            std::lock_guard<std::mutex> lk(m_acceptLock);
            
            if (m_acceptQ.empty())
            {
                break;  // 큐가 비었으면 종료
            }
            
            s = m_acceptQ.front();
            m_acceptQ.pop();
            
            printf("[DEBUG] Worker[%d] drainAccepted: Dequeued socket (socket=%llu, remaining=%zu)\n", 
                   m_index, (unsigned long long)s, m_acceptQ.size());
        }
        // 락 해제됨: 세션 생성 중에도 다른 스레드가 큐에 추가 가능
        
        printf("[DEBUG] Worker[%d] drainAccepted: Creating session (socket=%llu)\n", 
               m_index, (unsigned long long)s);
        
        // Session 객체 생성
        // - 생성자: 소켓, 워커 포인터, 버퍼풀 포인터 전달
        auto sess = std::make_unique<Session>(s, this, &m_pool);
        
        // 세션 시작 (RIO Request Queue 생성 및 초기 Recv 등록)
        if (!sess->start()) 
        {
            printf("[ERROR] Worker[%d] drainAccepted: Session start failed (socket=%llu)\n", 
                   m_index, (unsigned long long)s);
            closesocket(s);  // 실패 시 소켓 닫기
            
            continue;  // 다음 소켓 처리
        }
        
        printf("[DEBUG] Worker[%d] drainAccepted: Session created (socket=%llu, total=%zu)\n", 
               m_index, (unsigned long long)s, m_sessions.size() + 1);
        
        // 세션을 워커의 관리 리스트에 추가
        // - unique_ptr이므로 소유권 이전
        // - 세션 종료 시 자동 삭제 (RAII)
        m_sessions.emplace_back(std::move(sess));
    }
}

// ============================================================
// RIO Completion Queue에서 완료된 I/O 처리
// 
// Completion Queue를 폴링하여 완료된 I/O 작업들을 가져오고
// 각 작업에 대한 콜백을 호출합니다.
// 
// 동작 과정:
// 1. RIODequeueCompletion 호출 (최대 512개 결과 가져오기)
// 2. 각 결과에 대해:
//    a. RequestContext에서 ExtRioBuf 포인터 추출
//    b. owner 필드에서 Session 포인터 추출
//    c. op 필드로 Recv/Send 판별
//    d. 적절한 콜백 호출 (onRecvComplete / onSendComplete)
// 
// 참고: Zero-Copy 방식으로 데이터를 직접 버퍼풀에서 읽음
// ============================================================
void RIOWorker::handleCompletions() 
{
    // 완료 결과를 담을 배열 (스택 할당, 빠름)
    RIORESULT results[512];
    
    // Completion Queue에서 완료된 I/O 결과 가져오기
    // - 최대 512개까지 한 번에 가져옴 (배치 처리)
    // - 반환값: 실제 가져온 개수
    ULONG n = RIONetwork::Rio().RIODequeueCompletion(m_cq, results, 512);
    
    if (n == 0)
    {
        return;  // 완료된 작업 없음
    }
    
    // Completion Queue가 손상된 경우 (드물게 발생)
    if (n == RIO_CORRUPT_CQ) 
    {
        printf("[FATAL] RIO_CORRUPT_CQ on worker %d\n", m_index);
        
        return;
    }
    
    // 완료된 각 I/O 작업 처리
    for (ULONG i = 0; i < n; ++i) 
    {
        auto& r = results[i];
        
        // RequestContext를 ExtRioBuf로 캐스팅
        // (RIOReceive/RIOSend 호출 시 마지막 매개변수로 전달했던 값)
        auto* eb = reinterpret_cast<ExtRioBuf*>(r.RequestContext);
        Session* s = eb->owner;  // 이 I/O를 요청한 세션
        
        if (!s)
        {
            continue;  // 세션이 이미 종료됨
        }
        
        // 작업 타입에 따라 적절한 콜백 호출
        if (eb->op == OpType::Recv)
        {
            // Recv 완료: 데이터 수신 완료
            // - BytesTransferred: 실제 수신된 바이트 수
            // - Status: I/O 작업 상태 (NO_ERROR이면 성공)
            s->onRecvComplete(eb, r.BytesTransferred, r.Status);
        }
        else
        {
            // Send 완료: 데이터 전송 완료
            s->onSendComplete(eb, r.BytesTransferred, r.Status);
        }
    }
}

// ============================================================
// 워커 메인 루프
// 
// 워커 스레드의 핵심 함수로, 무한 루프를 돌며 다음 작업을 수행:
// 1. Accept 큐 확인 및 새 세션 생성
// 2. Completion Queue 폴링 및 I/O 완료 처리
// 3. CPU 효율적인 대기 전략 (Busy-Wait vs Sleep)
// 
// 대기 전략:
// - 폴링 모드: 짧은 Busy-Wait 후 짧은 Sleep (낮은 지연시간)
// - IOCP 모드: 적절한 Sleep (낮은 CPU 사용률)
// - 적응형: 유휴 시간이 길면 더 긴 Sleep
// ============================================================
void RIOWorker::run() 
{
    printf("[WORKER %d] Started (polling=%s)\n", m_index, Config::UsePolling ? "enabled" : "disabled");
    
    int spin = 0;        // Busy-Wait 카운터
    int idleCount = 0;   // 유휴 상태 카운터 (작업 없는 루프 횟수)
    
    // 메인 루프: m_run이 false가 될 때까지 계속 실행
    while (m_run.load(std::memory_order_relaxed)) 
    {
        // ========== 1. Accept 큐 확인 및 처리 ==========
        
        bool hasWork = false;
        {
            // 짧은 락: 큐가 비었는지만 확인
            std::lock_guard<std::mutex> lk(m_acceptLock);
            hasWork = !m_acceptQ.empty();
        }
        
        if (hasWork) 
        {
            // 큐에 소켓이 있으면 모두 처리 (세션 생성)
            drainAccepted();
            idleCount = 0;  // 작업이 있었으므로 유휴 카운터 리셋
        }
        
        // ========== 2. Completion Queue 확인 및 처리 ==========
        
        // I/O 완료 처리 전 큐 상태 확인 (빠른 체크)
        // - nullptr, 0: 개수만 확인 (실제 결과는 가져오지 않음)
        // - 반환값: 대기 중인 완료 통지 개수
        ULONG preCheck = RIONetwork::Rio().RIODequeueCompletion(m_cq, nullptr, 0);
        
        if (preCheck > 0 && preCheck != RIO_CORRUPT_CQ) 
        {
            // 완료된 I/O가 있으면 처리
            handleCompletions();
            idleCount = 0;  // 작업이 있었으므로 유휴 카운터 리셋
        } 
        else 
        {
            // 완료된 I/O가 없음
            idleCount++;
        }

        // ========== 3. 대기 전략 - CPU 효율과 응답 시간의 균형 ==========
        
        if (Config::UsePolling) 
        {
            // 폴링 모드: 낮은 지연시간을 위해 적극적으로 체크
            
            if (idleCount > 10) 
            {
                // 10번 이상 작업이 없으면 더 긴 Sleep (CPU 절약)
                Sleep(1);  // 약 1ms (타이머 해상도에 따라 다름)
                spin = 0;
            }
            else if (++spin >= Config::PollBusySpinIters) 
            {
                // Busy-Wait 횟수 제한 도달
                spin = 0;
                
                if (Config::PollSleepMicros > 0) 
                {
                    // 다른 스레드에게 양보 (컨텍스트 스위칭 허용)
                    Sleep(0);
                } 
                else 
                {
                    // CPU pause 명령어만 실행 (매우 짧은 대기)
                    cpu_relax();
                }
            } 
            else 
            {
                // 계속 Busy-Wait (가장 낮은 지연시간)
                cpu_relax();
            }
        } 
        else 
        {
            // IOCP 모드: CPU 사용률 최소화
            
            if (idleCount > 5) 
            {
                // 5번 이상 작업이 없으면 1ms Sleep
                Sleep(1);
            } 
            else 
            {
                // 다른 스레드에게 양보
                Sleep(0);
            }
        }
    }
    
    printf("[WORKER %d] Stopped\n", m_index);
}
