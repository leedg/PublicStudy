// UTF-8 인코딩 워닝 억제
#pragma warning(disable: 4566)

// Winsock deprecated 워닝 억제
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include "RIONetwork.h"
#include "Config.h"
#include <cstdio>

#ifndef SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER
#define SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER 0xC8000024
#endif

#ifndef WSAID_MULTIPLE_RIO
// {8509E081-96DD-4005-B165-9E2EE8C79E3F}
static const GUID WSAID_MULTIPLE_RIO =
{ 0x8509e081, 0x96dd, 0x4005,{ 0xb1, 0x65, 0x9e, 0x2e, 0xe8, 0xc7, 0x9e, 0x3f } };
#endif

// ============================================================
// RIO 네트워크 초기화
// ============================================================
bool RIONetwork::Init() 
{
    if (s_inited.load())
    {
        return true;
    }

    // Winsock 초기화
    if (WSAStartup(MAKEWORD(2,2), &s_wsa) != 0) 
    {
        printf("[FATAL] WSAStartup failed\n");
        
        return false;
    }

    // RIO 함수 테이블 로드를 위한 임시 소켓 생성
    SOCKET tmp = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_REGISTERED_IO);
    
    if (tmp == INVALID_SOCKET) 
    {
        printf("[FATAL] WSASocket temporary socket creation failed\n");
        
        return false;
    }
    
    // RIO 함수 테이블 가져오기
    DWORD bytes = 0;
    GUID gid = WSAID_MULTIPLE_RIO;
    
    if (WSAIoctl(tmp, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
                 &gid, sizeof(gid), &s_rio.tbl, sizeof(s_rio.tbl), &bytes, NULL, NULL) != 0) 
    {
        printf("[FATAL] WSAIoctl RIO table load failed (error=%lu)\n", GetLastError());
        closesocket(tmp);
        
        return false;
    }
    
    closesocket(tmp);
    s_rio.loaded = true;

    // IOCP 기반 CQ 알림용 (폴링 모드에서도 생성 가능)
    s_rioIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    s_rioOv   = (OVERLAPPED*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(OVERLAPPED));

    s_inited.store(true);
    
    return true;
}

// ============================================================
// RIO 네트워크 종료
// ============================================================
void RIONetwork::Shutdown() 
{
    if (!s_inited.load())
    {
        return;
    }
    
    // IOCP 핸들 정리
    if (s_rioIocp)
    {
        CloseHandle(s_rioIocp);
    }
    
    s_rioIocp = NULL;
    
    // OVERLAPPED 구조체 정리
    if (s_rioOv) 
    {
        HeapFree(GetProcessHeap(), 0, s_rioOv);
        s_rioOv = nullptr;
    }
    
    // Winsock 정리
    WSACleanup();
    s_inited.store(false);
}

// ============================================================
// RIO 함수 테이블 반환
// ============================================================
RIO_EXTENSION_FUNCTION_TABLE& RIONetwork::Rio() 
{
    return s_rio.tbl;
}

// ============================================================
// RIO Completion Queue 생성
// 
// size: CQ 크기
// outEventHandle: 이벤트 핸들 출력 (폴링 모드에서 사용)
// ============================================================
RIO_CQ RIONetwork::CreateCQ(uint32_t size, HANDLE& outEventHandle) 
{
    RIO_NOTIFICATION_COMPLETION nc{};
    
    if (Config::UsePolling) 
    {
        // 폴링 모드: 이벤트 기반 알림
        nc.Type = RIO_EVENT_COMPLETION;
        outEventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
        nc.Event.EventHandle = outEventHandle;
        nc.Event.NotifyReset = TRUE;
    } 
    else 
    {
        // IOCP 모드: IOCP 기반 알림
        nc.Type = RIO_IOCP_COMPLETION;
        nc.Iocp.IocpHandle = s_rioIocp;
        nc.Iocp.Overlapped = s_rioOv;
        nc.Iocp.CompletionKey = (PVOID)1;
        outEventHandle = NULL;
    }
    
    // Completion Queue 생성
    RIO_CQ cq = RIONetwork::Rio().RIOCreateCompletionQueue(size, &nc);
    
    if (cq == RIO_INVALID_CQ) 
    {
        printf("[FATAL] RIOCreateCompletionQueue failed (error=%lu)\n", GetLastError());
    }
    
    return cq;
}
