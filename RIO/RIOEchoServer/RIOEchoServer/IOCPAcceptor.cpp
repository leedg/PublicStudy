// UTF-8 인코딩 워닝 억제 (C4566: 유니코드 문자 표현)
#pragma warning(disable: 4566)

// Winsock deprecated 워닝 억제 (WSASocketA 등)
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "IOCPAcceptor.h"
#include <ws2tcpip.h>
#include <cstdio>

IOCPAcceptor::IOCPAcceptor(uint16_t port, std::vector<std::unique_ptr<RIOWorker>>& workers)
: m_port(port), m_workers(&workers) {}

IOCPAcceptor::~IOCPAcceptor() 
{ 
    stop(); 
}

// ============================================================
// AcceptEx 및 GetAcceptExSockaddrs 함수 포인터 로드
// ============================================================
void IOCPAcceptor::loadAcceptEx(SOCKET s) 
{
    DWORD bytes = 0;
    GUID g1 = WSAID_ACCEPTEX;
    GUID g2 = WSAID_GETACCEPTEXSOCKADDRS;
    
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &g1, sizeof(g1),
        &pAcceptEx, sizeof(pAcceptEx), &bytes, NULL, NULL) != 0) 
    {
        printf("[FATAL] AcceptEx load failed\n");
    }
    
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &g2, sizeof(g2),
        &pGetSockaddrs, sizeof(pGetSockaddrs), &bytes, NULL, NULL) != 0) 
    {
        printf("[FATAL] GetAcceptExSockaddrs load failed\n");
    }
}

// ============================================================
// Acceptor 시작
// ============================================================
bool IOCPAcceptor::start() 
{
    // Listen 소켓 생성
    m_listen = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    
    if (m_listen == INVALID_SOCKET) 
    { 
        printf("[FATAL] Listen socket creation failed\n");
        
        return false;
    }

    // 소켓 주소 설정
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    // 소켓 바인딩
    if (bind(m_listen, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) 
    {
        printf("[FATAL] Bind failed (error=%lu)\n", GetLastError());
        
        return false;
    }
    
    // Listen 모드로 전환
    if (listen(m_listen, Config::Backlog) == SOCKET_ERROR) 
    {
        printf("[FATAL] Listen failed\n");
        
        return false;
    }

    // AcceptEx 함수 포인터 로드
    loadAcceptEx(m_listen);

    // IOCP 생성
    m_iocp = CreateIoCompletionPort((HANDLE)m_listen, NULL, 0, 0);
    
    if (!m_iocp) 
    {
        printf("[FATAL] IOCP creation failed\n");
        
        return false;
    }

    // AcceptEx 풀 생성 및 대기 시작
    m_pool.reserve(Config::MaxPendingAccept);
    
    for (int i = 0; i < Config::MaxPendingAccept; ++i) 
    {
        auto* aov = new AcceptOv();
        postOneAccept(aov);
        m_pool.push_back(aov);
    }

    // Acceptor 스레드 시작
    m_run.store(true);
    m_thr = std::thread(&IOCPAcceptor::threadProc, this);
    
    printf("[ACCEPT] Started on port %u (pending=%d)\n", m_port, Config::MaxPendingAccept);
    
    return true;
}

// ============================================================
// Acceptor 중지
// ============================================================
void IOCPAcceptor::stop() 
{
    bool expected = true;
    
    if (!m_run.compare_exchange_strong(expected, false))
    {
        return;
    }
    
    if (m_thr.joinable())
    {
        PostQueuedCompletionStatus(m_iocp, 0, 0, NULL);
        m_thr.join();
    }

    // AcceptEx 풀 정리
    for (auto* a : m_pool) 
    {
        if (a->acceptSock != INVALID_SOCKET)
        {
            closesocket(a->acceptSock);
        }
        
        delete a;
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
// ============================================================
void IOCPAcceptor::postOneAccept(AcceptOv* aov) 
{
    // Accept될 소켓 생성 (RIO 플래그로 생성)
    aov->acceptSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_REGISTERED_IO);
    
    // *** Breakpoint 1: Before AcceptEx call ***
    printf("[DEBUG] postOneAccept: Starting AcceptEx call (socket=%llu)\n", (unsigned long long)aov->acceptSock);
    
    DWORD bytes = 0;
    ZeroMemory((OVERLAPPED*)aov, sizeof(OVERLAPPED));
    
    // AcceptEx 호출 - 비동기 연결 수락
    BOOL ok = pAcceptEx(m_listen, aov->acceptSock, aov->addrBuf, 0,
                        sizeof(sockaddr_in)+16, sizeof(sockaddr_in)+16, &bytes, (OVERLAPPED*)aov);
    
    if (!ok) 
    {
        int e = WSAGetLastError();
        
        if (e != ERROR_IO_PENDING) 
        {
            printf("[ERROR] AcceptEx post failed (error=%d)\n", e);
        } 
        else 
        {
            printf("[DEBUG] postOneAccept: AcceptEx pending\n");
        }
    } 
    else 
    {
        printf("[DEBUG] postOneAccept: AcceptEx completed immediately\n");
    }
}

// ============================================================
// Acceptor 스레드 프로시저 - 연결 수락 처리
// ============================================================
void IOCPAcceptor::threadProc() 
{
    DWORD bytes=0; 
    ULONG_PTR key=0; 
    LPOVERLAPPED ov=nullptr;
    
    while (m_run.load(std::memory_order_relaxed)) 
    {
        // IOCP에서 완료 알림 대기 (타임아웃 1초)
        BOOL ok = GetQueuedCompletionStatus(m_iocp, &bytes, &key, &ov, 1000);
        
        if (!ok) 
        {
            DWORD e = GetLastError();
            
            if (ov == nullptr) 
            {
                if (e == WAIT_TIMEOUT)
                {
                    continue;
                }
                
                continue;
            }
        }
        
        if (!ov)
        {
            continue;
        }
        
        // *** Breakpoint 2: Accept completion received ***
        printf("[DEBUG] threadProc: Accept completed (bytes=%lu)\n", bytes);
        
        AcceptOv* aov = reinterpret_cast<AcceptOv*>(ov);
        
        // *** Breakpoint 3: Setting up accepted socket ***
        printf("[DEBUG] threadProc: Setting up accepted socket (socket=%llu)\n", (unsigned long long)aov->acceptSock);

        // Accept된 소켓을 IOCP에 연결
        CreateIoCompletionPort((HANDLE)aov->acceptSock, m_iocp, 0, 0);
        
        // Accept 컨텍스트 업데이트
        setsockopt(aov->acceptSock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&m_listen, sizeof(m_listen));

        // Round-Robin 방식으로 워커 선택
        uint32_t idx = m_rr.fetch_add(1) % (uint32_t)m_workers->size();
        
        // *** Breakpoint 4: Forwarding socket to worker ***
        printf("[DEBUG] threadProc: Forwarding socket to worker[%u] (socket=%llu)\n", idx, (unsigned long long)aov->acceptSock);
        
        (*m_workers)[idx]->enqueueAccepted(aov->acceptSock);

        // *** Breakpoint 5: Starting next Accept wait ***
        printf("[DEBUG] threadProc: Starting next Accept wait\n\n");
        
        postOneAccept(aov);
    }
}
