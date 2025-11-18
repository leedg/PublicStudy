// UTF-8 인코딩 워닝 억제 (C4566: 유니코드 문자 표현)
#pragma warning(disable: 4566)

// Winsock deprecated 워닝 억제 (WSASocketA 등)
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "IOCPAcceptor.h"
#include <ws2tcpip.h>
#include <cstdio>

// ============================================================
// IOCPAcceptor 생성자
// 
// 클라이언트 연결을 수락하는 역할을 담당합니다.
// AcceptEx를 사용한 비동기 연결 수락으로 높은 성능 제공
// 
// 매개변수:
//   - port: 리스닝할 포트 번호
//   - workers: RIOWorker 객체들의 벡터 (Round-Robin 분배용)
// ============================================================
IOCPAcceptor::IOCPAcceptor(uint16_t port, std::vector<std::unique_ptr<RIOWorker>>& workers)
: m_port(port), m_workers(&workers) {}

// ============================================================
// IOCPAcceptor 소멸자
// ============================================================
IOCPAcceptor::~IOCPAcceptor() 
{ 
    stop(); 
}

// ============================================================
// AcceptEx 및 GetAcceptExSockaddrs 함수 포인터 로드
// 
// Windows에서 AcceptEx는 확장 함수로 제공되므로
// 런타임에 함수 포인터를 동적으로 가져와야 합니다.
// 
// AcceptEx란?
// - 일반 accept()의 비동기 버전
// - IOCP와 함께 사용하여 높은 성능 제공
// - 연결 수락과 동시에 첫 데이터도 받을 수 있음 (여기선 사용 안 함)
// 
// 매개변수:
//   - s: 리스닝 소켓 핸들 (WSAIoctl 호출용)
// ============================================================
void IOCPAcceptor::loadAcceptEx(SOCKET s) 
{
    DWORD bytes = 0;
    GUID g1 = WSAID_ACCEPTEX;
    GUID g2 = WSAID_GETACCEPTEXSOCKADDRS;
    
    // AcceptEx 함수 포인터 가져오기
    // WSAIoctl: 소켓 제어 명령 실행
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &g1, sizeof(g1),
        &pAcceptEx, sizeof(pAcceptEx), &bytes, NULL, NULL) != 0) 
    {
        printf("[FATAL] AcceptEx load failed\n");
    }
    
    // GetAcceptExSockaddrs 함수 포인터 가져오기
    // (클라이언트/서버 주소 정보 추출용, 여기선 사용 안 함)
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &g2, sizeof(g2),
        &pGetSockaddrs, sizeof(pGetSockaddrs), &bytes, NULL, NULL) != 0) 
    {
        printf("[FATAL] GetAcceptExSockaddrs load failed\n");
    }
}

// ============================================================
// Acceptor 시작
// 
// 리스닝 소켓을 생성하고 여러 개의 AcceptEx를 미리 등록합니다.
// 
// 동작 과정:
// 1. TCP 리스닝 소켓 생성
// 2. 소켓을 포트에 바인딩
// 3. Listen 모드로 전환 (연결 대기 큐 생성)
// 4. AcceptEx 함수 포인터 로드
// 5. IOCP 생성 (완료 통지 수신용)
// 6. 여러 개의 AcceptEx 요청을 미리 등록 (동시 연결 처리)
// 7. Acceptor 스레드 시작 (완료 통지 처리)
// 
// 왜 여러 개의 AcceptEx를 등록하는가?
// - 동시에 여러 클라이언트가 연결 시도할 수 있음
// - 미리 등록해두면 즉시 처리 가능 (대기 시간 최소화)
// - 예: MaxPendingAccept=10이면 최대 10개 동시 연결 처리
// 
// 반환값: 성공 시 true, 실패 시 false
// ============================================================
bool IOCPAcceptor::start() 
{
    // Listen 소켓 생성
    // WSA_FLAG_OVERLAPPED: 비동기 I/O 지원 (IOCP 사용 가능)
    m_listen = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    
    if (m_listen == INVALID_SOCKET) 
    { 
        printf("[FATAL] Listen socket creation failed\n");
        
        return false;
    }

    // 소켓 주소 설정
    sockaddr_in addr{};
    addr.sin_family = AF_INET;           // IPv4
    addr.sin_port = htons(m_port);       // 포트 번호 (네트워크 바이트 순서로 변환)
    addr.sin_addr.s_addr = INADDR_ANY;   // 모든 네트워크 인터페이스에서 수신
    
    // 소켓 바인딩 (IP:Port에 소켓 연결)
    if (bind(m_listen, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) 
    {
        printf("[FATAL] Bind failed (error=%lu)\n", GetLastError());
        
        return false;
    }
    
    // Listen 모드로 전환
    // Backlog: 연결 대기 큐 크기 (클라이언트가 대기할 수 있는 최대 개수)
    if (listen(m_listen, Config::Backlog) == SOCKET_ERROR) 
    {
        printf("[FATAL] Listen failed\n");
        
        return false;
    }

    // AcceptEx 함수 포인터 로드
    loadAcceptEx(m_listen);

    // IOCP 생성 (I/O Completion Port)
    // - 비동기 I/O 완료 통지를 받기 위한 커널 객체
    // - 여러 소켓의 완료 통지를 하나의 핸들로 관리
    // - 스레드 풀 기반 효율적인 처리 가능
    m_iocp = CreateIoCompletionPort((HANDLE)m_listen, NULL, 0, 0);
    
    if (!m_iocp) 
    {
        printf("[FATAL] IOCP creation failed\n");
        
        return false;
    }

    // AcceptEx 풀 생성 및 대기 시작
    // 여러 개의 AcceptEx를 미리 등록하여 동시 연결 처리
    m_pool.reserve(Config::MaxPendingAccept);
    
    for (int i = 0; i < Config::MaxPendingAccept; ++i) 
    {
        // AcceptOv: OVERLAPPED 구조체 + Accept 소켓 정보
        auto* aov = new AcceptOv();
        postOneAccept(aov);           // AcceptEx 요청 등록
        m_pool.push_back(aov);        // 나중에 정리하기 위해 보관
    }

    // Acceptor 스레드 시작
    // - threadProc() 함수가 별도 스레드에서 실행됨
    // - IOCP에서 완료 통지를 받아 처리
    m_run.store(true);
    m_thr = std::thread(&IOCPAcceptor::threadProc, this);
    
    printf("[ACCEPT] Started on port %u (pending=%d)\n", m_port, Config::MaxPendingAccept);
    
    return true;
}

// ============================================================
// Acceptor 중지
// 
// Acceptor를 안전하게 종료하고 모든 리소스를 정리합니다.
// 
// 정리 순서:
// 1. m_run 플래그를 false로 설정 (스레드 종료 신호)
// 2. IOCP에 더미 완료 통지를 보내 스레드를 깨움
// 3. 스레드 종료 대기
// 4. AcceptEx 풀의 모든 소켓 닫기
// 5. IOCP 핸들 닫기
// 6. 리스닝 소켓 닫기
// ============================================================
void IOCPAcceptor::stop() 
{
    bool expected = true;
    
    // Compare-And-Swap으로 중복 호출 방지
    if (!m_run.compare_exchange_strong(expected, false))
    {
        return;
    }
    
    // 스레드 종료 처리
    if (m_thr.joinable())
    {
        // 더미 완료 통지를 보내 GetQueuedCompletionStatus를 깨움
        // (타임아웃 대기 중인 스레드를 즉시 종료하기 위해)
        PostQueuedCompletionStatus(m_iocp, 0, 0, NULL);
        m_thr.join();  // 스레드 종료 대기
    }

    // AcceptEx 풀 정리
    // 대기 중인 모든 Accept 소켓 닫기
    for (auto* a : m_pool) 
    {
        if (a->acceptSock != INVALID_SOCKET)
        {
            closesocket(a->acceptSock);
        }
        
        delete a;  // AcceptOv 구조체 해제
    }
    
    m_pool.clear();

    // IOCP 및 Listen 소켓 정리
    if (m_iocp)
    {
        CloseHandle(m_iocp);
        m_iocp = NULL;
    }
    
    if (m_listen != INVALID_SOCKET)
    {
        closesocket(m_listen);
        m_listen = INVALID_SOCKET;
    }
}

// ============================================================
// AcceptEx 요청 등록
// 
// 클라이언트 연결을 수락하기 위한 비동기 요청을 등록합니다.
// 
// 매개변수:
//   - aov: AcceptOv 구조체 포인터 (OVERLAPPED + Accept 소켓 정보)
// 
// 동작 과정:
// 1. Accept될 소켓을 미리 생성 (WSA_FLAG_REGISTERED_IO)
// 2. OVERLAPPED 구조체 초기화
// 3. AcceptEx 호출 (비동기 연결 수락)
// 4. 클라이언트 연결 시 IOCP로 완료 통지
// 
// 왜 Accept 소켓을 미리 생성하는가?
// - AcceptEx는 Accept될 소켓을 매개변수로 받음
// - 연결 성공 시 이 소켓이 클라이언트와 연결됨
// - 일반 accept()는 커널이 소켓을 생성하지만 AcceptEx는 사용자가 생성
// 
// WSA_FLAG_REGISTERED_IO 플래그:
// - RIO API를 사용할 수 있는 소켓으로 생성
// - 이 소켓은 나중에 RIOWorker로 전달되어 RIO I/O에 사용됨
// ============================================================
void IOCPAcceptor::postOneAccept(AcceptOv* aov) 
{
    // Accept될 소켓 생성 (RIO 플래그로 생성)
    // 이 소켓은 연결 성공 시 클라이언트와 연결됨
    aov->acceptSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_REGISTERED_IO);
    
    printf("[DEBUG] postOneAccept: Starting AcceptEx call (socket=%llu)\n", (unsigned long long)aov->acceptSock);
    
    DWORD bytes = 0;
    // OVERLAPPED 구조체 초기화 (비동기 I/O 식별용)
    ZeroMemory((OVERLAPPED*)aov, sizeof(OVERLAPPED));
    
    // AcceptEx 호출 - 비동기 연결 수락
    // 매개변수:
    //   - m_listen: 리스닝 소켓
    //   - aov->acceptSock: Accept될 소켓 (미리 생성)
    //   - aov->addrBuf: 주소 정보 버퍼 (클라이언트/서버 주소 저장용, 여기선 사용 안 함)
    //   - 0: 첫 데이터를 받지 않음 (연결만 수락)
    //   - sizeof(sockaddr_in)+16: 로컬 주소 버퍼 크기
    //   - sizeof(sockaddr_in)+16: 원격 주소 버퍼 크기
    //   - &bytes: 수신된 바이트 수 (동기 완료 시)
    //   - (OVERLAPPED*)aov: 비동기 작업 식별자
    BOOL ok = pAcceptEx(m_listen, aov->acceptSock, aov->addrBuf, 0,
                        sizeof(sockaddr_in)+16, sizeof(sockaddr_in)+16, &bytes, (OVERLAPPED*)aov);
    
    if (!ok) 
    {
        int e = WSAGetLastError();
        
        if (e != ERROR_IO_PENDING) 
        {
            // 실제 에러 발생 (메모리 부족 등)
            printf("[ERROR] AcceptEx post failed (error=%d)\n", e);
        } 
        else 
        {
            // ERROR_IO_PENDING: 정상적인 비동기 대기 상태
            // 클라이언트 연결 시 IOCP로 완료 통지가 옴
            printf("[DEBUG] postOneAccept: AcceptEx pending\n");
        }
    } 
    else 
    {
        // AcceptEx가 즉시 완료됨 (드물게 발생)
        // IOCP로 완료 통지가 이미 전달됨
        printf("[DEBUG] postOneAccept: AcceptEx completed immediately\n");
    }
}

// ============================================================
// Acceptor 스레드 프로시저 - 연결 수락 처리
// 
// IOCP에서 AcceptEx 완료 통지를 받아 처리하는 메인 루프입니다.
// 
// 동작 과정:
// 1. GetQueuedCompletionStatus로 IOCP 대기 (타임아웃 5초)
// 2. 완료 통지 수신 시:
//    a. Accept된 소켓 설정 (IOCP 연결, 컨텍스트 업데이트)
//    b. Round-Robin 방식으로 워커 선택
//    c. 선택된 워커에게 소켓 전달
//    d. 새로운 AcceptEx 요청 등록 (다음 연결 대기)
// 3. 타임아웃 시 루프 계속 (종료 플래그 확인)
// 
// Round-Robin 부하 분산:
// - 각 워커에게 순차적으로 소켓을 분배
// - 간단하지만 효과적인 부하 분산 전략
// - 예: Worker 0 -> Worker 1 -> Worker 2 -> Worker 0 -> ...
// ============================================================
void IOCPAcceptor::threadProc() 
{
    DWORD bytes=0;           // 전송된 바이트 수
    ULONG_PTR key=0;         // Completion Key (여기선 사용 안 함)
    LPOVERLAPPED ov=nullptr; // OVERLAPPED 포인터 (AcceptOv로 캐스팅)
    
    while (m_run.load(std::memory_order_relaxed)) 
    {
        // IOCP에서 완료 알림 대기 (타임아웃 5초)
        // - 타임아웃이 길면 종료 응답성이 낮지만 CPU 사용률이 낮음
        // - 5초는 균형잡힌 값 (빠른 종료 + 낮은 CPU)
        BOOL ok = GetQueuedCompletionStatus(m_iocp, &bytes, &key, &ov, 5000);
        
        if (!ok) 
        {
            DWORD e = GetLastError();
            
            if (ov == nullptr) 
            {
                // ov가 null: 타임아웃 또는 치명적 에러
                if (e == WAIT_TIMEOUT)
                {
                    continue;  // 타임아웃 - 루프 계속 (종료 플래그 확인)
                }
                
                continue;  // 기타 에러 - 루프 계속
            }
            // ov가 null이 아니면: 특정 I/O 작업 실패
            // (여기선 처리 안 함, 실제로는 로그 남기거나 소켓 닫기)
        }
        
        if (!ov)
        {
            continue;  // ov가 null이면 처리할 작업 없음
        }
        
        printf("[DEBUG] threadProc: Accept completed (bytes=%lu)\n", bytes);
        
        // OVERLAPPED를 AcceptOv로 캐스팅
        // (postOneAccept에서 전달한 aov 포인터)
        AcceptOv* aov = reinterpret_cast<AcceptOv*>(ov);
        
        printf("[DEBUG] threadProc: Setting up accepted socket (socket=%llu)\n", (unsigned long long)aov->acceptSock);

        // Accept된 소켓을 IOCP에 연결 (아직 사용 안 하지만 미리 연결)
        // 나중에 일반 Winsock I/O를 사용한다면 이 IOCP로 통지받음
        CreateIoCompletionPort((HANDLE)aov->acceptSock, m_iocp, 0, 0);
        
        // Accept 컨텍스트 업데이트
        // AcceptEx로 Accept된 소켓은 리스닝 소켓의 속성을 상속받지 않음
        // 이 옵션을 설정해야 getsockname 등이 제대로 동작함
        setsockopt(aov->acceptSock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&m_listen, sizeof(m_listen));

        // Round-Robin 방식으로 워커 선택
        // - m_rr: 원자적 카운터 (스레드 안전)
        // - fetch_add(1): 현재 값 반환 후 1 증가
        // - % workers->size(): 워커 개수로 나눈 나머지 (순환)
        uint32_t idx = m_rr.fetch_add(1) % (uint32_t)m_workers->size();
        
        printf("[DEBUG] threadProc: Forwarding socket to worker[%u] (socket=%llu)\n", idx, (unsigned long long)aov->acceptSock);
        
        // 선택된 워커에게 소켓 전달
        // - enqueueAccepted: 스레드 안전한 큐에 소켓 추가
        // - 워커 스레드가 나중에 이 소켓으로 세션을 생성
        (*m_workers)[idx]->enqueueAccepted(aov->acceptSock);

        printf("[DEBUG] threadProc: Starting next Accept wait\n\n");
        
        // 새로운 AcceptEx 요청 등록 (다음 연결 대기)
        // - 같은 AcceptOv 구조체 재사용
        // - 새로운 Accept 소켓 생성 및 등록
        // - 이렇게 해야 계속 연결을 수락할 수 있음
        postOneAccept(aov);
    }
}
