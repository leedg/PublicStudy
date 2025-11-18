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
// 
// 각 클라이언트 연결마다 Session 객체가 하나씩 생성됩니다.
// 
// 매개변수:
//   - s: 클라이언트와 연결된 소켓 핸들
//   - worker: 이 세션을 관리하는 RIOWorker 포인터
//   - pool: 송수신 버퍼를 관리하는 BufferPool 포인터
// ============================================================
Session::Session(SOCKET s, RIOWorker* worker, BufferPool* pool)
    : m_sock(s), m_worker(worker), m_pool(pool) 
{
    // 디버그: 세션 생성 로그
    printf("[DEBUG] Session constructor: socket=%llu\n", (unsigned long long)s);
}

// ============================================================
// 세션 소멸자
// 
// 세션 종료 시 자동으로 stop()을 호출하여 리소스를 정리합니다.
// ============================================================
Session::~Session() 
{ 
    stop(); 
}

// ============================================================
// 세션 시작 - RIO Request Queue 생성 및 Recv 등록
// 
// 이 함수는 다음 작업을 수행합니다:
// 1. RIO Request Queue 생성 (RIO의 핵심 구조체)
// 2. TCP_NODELAY 옵션 설정 (낮은 지연시간을 위해)
// 3. 초기 Recv 요청 등록 (데이터 수신 준비)
// 
// RIO Request Queue란?
//   - 각 소켓마다 하나씩 생성되는 I/O 요청 큐
//   - Send/Recv 요청을 비동기적으로 처리
//   - Completion Queue와 연결되어 완료 통지를 받음
// 
// 반환값: 성공 시 true, 실패 시 false
// ============================================================
bool Session::start() 
{
    printf("[DEBUG] Session::start: Creating RIORequestQueue (socket=%llu)\n", 
           (unsigned long long)m_sock);
    
    // 세션 활성 상태로 설정 (원자적 연산)
    m_alive.store(true);
    
    // RIO Request Queue 생성
    // 매개변수:
    //   - m_sock: 소켓 핸들
    //   - RecvOutstandingPerSession: 동시 대기 가능한 Recv 요청 수
    //   - 1: Recv 데이터 버퍼 수 (RIO_BUF 배열 크기)
    //   - SendOutstandingPerSession: 동시 대기 가능한 Send 요청 수
    //   - 1: Send 데이터 버퍼 수 (RIO_BUF 배열 크기)
    //   - cq: Send/Recv 완료 통지를 받을 Completion Queue
    //   - this: Request Context (완료 시 세션 식별용)
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

    printf("[DEBUG] Session::start: RIORequestQueue created (socket=%llu, rq=%llu)\n", 
           (unsigned long long)m_sock, (unsigned long long)m_rq);

    // TCP_NODELAY 옵션 설정 (Nagle 알고리즘 비활성화)
    // 이유: 에코 서버는 즉각적인 응답이 중요하므로 패킷 결합을 하지 않음
    if (Config::TcpNoDelay) 
    {
        DWORD on = 1;
        setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));
    }

    printf("[DEBUG] Session::start: Posting %d Recv requests (socket=%llu)\n", 
           Config::RecvOutstandingPerSession, (unsigned long long)m_sock);
    
    // 초기 Recv 요청 등록
    // 여러 개의 Recv를 미리 등록해두면 데이터 도착 시 즉시 처리 가능
    // (Zero-Copy 및 커널 오버헤드 감소)
    for (int i = 0; i < Config::RecvOutstandingPerSession; ++i) 
    {
        if (!postRecv())
        {
            break;  // 버퍼 부족 등의 이유로 실패 시 중단
        }
    }
    
    printf("[DEBUG] Session::start: Session started (socket=%llu)\n\n", (unsigned long long)m_sock);
    
    return true;
}

// ============================================================
// 세션 중지
// 
// 세션을 안전하게 종료하고 리소스를 정리합니다.
// Compare-And-Swap을 사용하여 중복 호출을 방지합니다.
// ============================================================
void Session::stop() 
{
    bool expected = true;
    
    // 원자적으로 상태를 확인하고 변경 (이미 false면 종료 진행 중)
    if (!m_alive.compare_exchange_strong(expected, false))
    {
        return;  // 이미 stop()이 호출되었으므로 중복 실행 방지
    }
    
    // 소켓 닫기 (연결 종료)
    if (m_sock != INVALID_SOCKET) 
    {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    
    // RIO Request Queue는 소켓이 닫히면 자동으로 무효화됨
    m_rq = RIO_INVALID_RQ;
}

// ============================================================
// Recv 요청 등록
// 
// 클라이언트로부터 데이터를 수신하기 위한 비동기 요청을 등록합니다.
// 
// 동작 과정:
// 1. BufferPool에서 버퍼 슬라이스(청크) 할당
// 2. ExtRioBuf 구조체 생성 및 초기화
// 3. RIOReceive 호출로 커널에 수신 요청 등록
// 4. 데이터 도착 시 Completion Queue를 통해 통지됨
// 
// 반환값: 성공 시 true, 실패 시 false
// ============================================================
bool Session::postRecv() 
{
    // 세션이 종료 중이면 새로운 Recv 요청을 등록하지 않음
    if (!m_alive.load())
    {
        return false;
    }
    
    // 버퍼 슬라이스 할당 (공유 버퍼 풀에서 청크 하나 가져오기)
    uint32_t off = 0;
    
    if (!m_pool->allocSlice(off))
    {
        return false;  // 버퍼 풀이 가득 참
    }

    // ExtRioBuf 생성 및 설정
    // RIO_BUF를 확장한 구조체로, 작업 타입과 소유자 정보 포함
    auto* eb = new ExtRioBuf();
    eb->op = OpType::Recv;              // 작업 타입: Recv
    eb->owner = this;                    // 이 세션을 가리킴
    eb->BufferId = m_pool->bufferId();   // 버퍼 풀 ID
    eb->Offset = off;                    // 버퍼 내 오프셋
    eb->Length = Config::SliceSize;      // 수신할 최대 바이트 수
    eb->sliceOffset = off;               // 버퍼 해제를 위한 오프셋 보관

    // RIO Receive 요청 등록
    // 매개변수:
    //   - m_rq: Request Queue 핸들
    //   - eb: RIO_BUF 배열 (크기 1)
    //   - 1: 버퍼 배열 크기
    //   - 0: 플래그 (일반적으로 0)
    //   - eb: RequestContext (완료 시 이 포인터를 받음)
    if (!RIONetwork::Rio().RIOReceive(m_rq, eb, 1, 0, eb)) 
    {
        // 등록 실패 시 리소스 정리
        m_pool->freeSlice(off);
        delete eb;
        
        return false;
    }
    
    // 진행 중인 Recv 요청 수 증가 (디버깅/모니터링용)
    m_inFlightRecv.fetch_add(1, std::memory_order_relaxed);
    
    return true;
}

// ============================================================
// Send 요청 등록
// 
// 클라이언트로 데이터를 전송하기 위한 비동기 요청을 등록합니다.
// 
// 매개변수:
//   - offset: 버퍼 풀 내 데이터의 시작 오프셋
//   - len: 전송할 바이트 수
// 
// 반환값: 성공 시 true, 실패 시 false
// ============================================================
bool Session::postSend(uint32_t offset, uint32_t len) 
{
    // 세션이 종료 중이면 새로운 Send 요청을 등록하지 않음
    if (!m_alive.load())
    {
        return false;
    }

    // ExtRioBuf 생성 및 설정
    auto* eb = new ExtRioBuf();
    eb->op = OpType::Send;               // 작업 타입: Send
    eb->owner = this;                     // 이 세션을 가리킴
    eb->BufferId = m_pool->bufferId();    // 버퍼 풀 ID
    eb->Offset = offset;                  // 버퍼 내 오프셋 (전송할 데이터 위치)
    eb->Length = len;                     // 전송할 바이트 수
    eb->sliceOffset = offset;             // 버퍼 해제를 위한 오프셋 보관

    // RIO Send 요청 등록
    // Recv와 유사하지만 이미 버퍼에 있는 데이터를 전송함
    if (!RIONetwork::Rio().RIOSend(m_rq, eb, 1, 0, eb)) 
    {
        // 등록 실패 시 리소스 정리
        m_pool->freeSlice(offset);
        delete eb;
        
        return false;
    }
    
    // 진행 중인 Send 요청 수 증가
    m_inFlightSend.fetch_add(1, std::memory_order_relaxed);
    
    return true;
}

// ============================================================
// Recv 완료 콜백 - 에코를 위해 받은 데이터를 그대로 Send
// 
// RIO Completion Queue에서 Recv 완료 통지를 받으면 호출됩니다.
// 
// 동작 과정:
// 1. 수신된 데이터 내용을 콘솔에 출력 (16진수 + 텍스트)
// 2. 같은 버퍼를 사용하여 에코 응답 전송 (Zero-Copy)
// 3. ExtRioBuf 구조체 해제 (버퍼는 Send 완료 후 해제)
// 
// 매개변수:
//   - eb: ExtRioBuf 포인터 (요청 시 전달한 RequestContext)
//   - bytes: 실제 수신된 바이트 수
//   - status: I/O 작업 상태 (NO_ERROR이면 성공)
// ============================================================
void Session::onRecvComplete(ExtRioBuf* eb, DWORD bytes, DWORD status) 
{
    // 진행 중인 Recv 요청 수 감소
    m_inFlightRecv.fetch_sub(1, std::memory_order_relaxed);
    
    // 에러 또는 연결 종료 체크
    // bytes == 0은 graceful shutdown (클라이언트가 연결 종료)
    if (!m_alive.load() || status != NO_ERROR || bytes == 0) 
    {
        m_pool->freeSlice(eb->sliceOffset);  // 버퍼 해제
        delete eb;                            // ExtRioBuf 구조체 해제
        stop();                               // 세션 종료
        return;
    }
    
    // ========== 받은 데이터 내용 출력 (디버깅/모니터링용) ==========
    
    // 버퍼 풀의 기준 주소 + 오프셋 = 실제 데이터 주소
    char* data = m_pool->base() + eb->sliceOffset;
    
    printf("\n[RECV] Socket=%llu Bytes=%lu\n", (unsigned long long)m_sock, bytes);
    
    // 16진수 출력 (최대 64바이트)
    // 네트워크 프로토콜 디버깅에 유용
    printf("  Hex: ");
    for (DWORD i = 0; i < bytes && i < 64; ++i) 
    {
        printf("%02X ", (unsigned char)data[i]);
    }
    
    // 텍스트 출력 (최대 256바이트)
    // 출력 가능한 ASCII 문자는 그대로, 제어 문자는 이스케이프 표시
    printf("\n  Text: \"");
    for (DWORD i = 0; i < bytes && i < 256; ++i) 
    {
        char c = data[i];
        if (c >= 32 && c <= 126)  // 출력 가능한 ASCII 범위
        {
            printf("%c", c);
        }
        else if (c == '\n')       // 줄바꿈
        {
            printf("\\n");
        }
        else if (c == '\r')       // 캐리지 리턴
        {
            printf("\\r");
        }
        else                      // 기타 제어 문자
        {
            printf(".");
        }
    }
    printf("\"\n");
    
    // ========== 에코: 받은 데이터를 그대로 전송 (Zero-Copy) ==========
    
    // 같은 버퍼(offset)를 재사용하여 전송 요청
    // 메모리 복사 없이 커널이 직접 전송 (성능 향상)
    if (!postSend(eb->sliceOffset, bytes)) 
    {
        // Send 등록 실패 시 버퍼 해제
        m_pool->freeSlice(eb->sliceOffset);
    }
    
    // ExtRioBuf 구조체 해제
    // 주의: 버퍼(slice)는 Send 완료 후 해제됨
    delete eb;
}

// ============================================================
// Send 완료 콜백 - 전송 완료 후 다시 Recv 등록
// 
// RIO Completion Queue에서 Send 완료 통지를 받으면 호출됩니다.
// 
// 동작 과정:
// 1. 사용했던 버퍼 슬라이스를 버퍼 풀에 반환
// 2. 다음 데이터 수신을 위해 새로운 Recv 요청 등록
// 
// 에코 서버 흐름:
//   Recv 요청 -> 데이터 수신 -> onRecvComplete -> Send 요청 -> 
//   데이터 전송 -> onSendComplete -> 버퍼 해제 -> 새로운 Recv 요청 (반복)
// 
// 매개변수:
//   - eb: ExtRioBuf 포인터
//   - bytes: 실제 전송된 바이트 수 (사용하지 않음)
//   - status: I/O 작업 상태
// ============================================================
void Session::onSendComplete(ExtRioBuf* eb, DWORD /*bytes*/, DWORD status) 
{
    // 진행 중인 Send 요청 수 감소
    m_inFlightSend.fetch_sub(1, std::memory_order_relaxed);
    
    // 버퍼 해제 (이제 다른 Recv/Send에서 재사용 가능)
    m_pool->freeSlice(eb->sliceOffset);
    delete eb;  // ExtRioBuf 구조체 해제
    
    // 에러 체크 (전송 실패 또는 세션 종료 중)
    if (!m_alive.load() || status != NO_ERROR) 
    {
        stop();  // 세션 종료
        
        return;
    }
    
    // 다음 데이터 수신을 위해 Recv 재등록
    // 이렇게 해야 클라이언트가 추가로 데이터를 보낼 때 받을 수 있음
    postRecv();
}
