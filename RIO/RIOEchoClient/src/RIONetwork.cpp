#include "RIONetwork.h"

#include <cstdio>

#ifndef SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER
#define SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER 0xC8000024
#endif

#ifndef WSAID_MULTIPLE_RIO
// {8509E081-96DD-4005-B165-9E2EE8C79E3F}
static const GUID WSAID_MULTIPLE_RIO =
{ 0x8509e081, 0x96dd, 0x4005,{ 0xb1, 0x65, 0x9e, 0x2e, 0xe8, 0xc7, 0x9e, 0x3f } };
#endif

bool RIONetwork::Init() {
    if (s_inited.load()) {
        return true;
    }

    if (WSAStartup(MAKEWORD(2, 2), &s_wsa) != 0) {
        printf("[CLIENT][FATAL] WSAStartup failed\n");
        return false;
    }

    SOCKET tmp = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_REGISTERED_IO);
    if (tmp == INVALID_SOCKET) {
        printf("[CLIENT][FATAL] WSASocket temporary socket creation failed\n");
        return false;
    }

    DWORD bytes = 0;
    GUID gid = WSAID_MULTIPLE_RIO;
    if (WSAIoctl(tmp,
                 SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
                 &gid, sizeof(gid),
                 &s_rio.table, sizeof(s_rio.table),
                 &bytes, NULL, NULL) != 0) {
        printf("[CLIENT][FATAL] WSAIoctl RIO table load failed (error=%lu)\n", GetLastError());
        closesocket(tmp);
        return false;
    }

    closesocket(tmp);
    s_rio.loaded = true;

    s_rioIocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    s_rioOv = static_cast<OVERLAPPED*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(OVERLAPPED)));

    s_inited.store(true);
    return true;
}

void RIONetwork::Shutdown() {
    if (!s_inited.load()) {
        return;
    }

    if (s_rioIocp) {
        CloseHandle(s_rioIocp);
        s_rioIocp = NULL;
    }

    if (s_rioOv) {
        HeapFree(GetProcessHeap(), 0, s_rioOv);
        s_rioOv = nullptr;
    }

    WSACleanup();
    s_inited.store(false);
}

RIO_EXTENSION_FUNCTION_TABLE& RIONetwork::Rio() {
    return s_rio.table;
}

RIO_CQ RIONetwork::CreateEventCQ(uint32_t size, HANDLE& outEventHandle) {
    RIO_NOTIFICATION_COMPLETION nc{};
    nc.Type = RIO_EVENT_COMPLETION;
    outEventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
    nc.Event.EventHandle = outEventHandle;
    nc.Event.NotifyReset = TRUE;

    RIO_CQ cq = RIONetwork::Rio().RIOCreateCompletionQueue(size, &nc);
    if (cq == RIO_INVALID_CQ) {
        printf("[CLIENT][FATAL] RIOCreateCompletionQueue failed (error=%lu)\n", GetLastError());
    }

    return cq;
}
