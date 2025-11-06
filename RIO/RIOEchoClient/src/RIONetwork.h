#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif

#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <atomic>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

struct RIO_EXTENSION_FUNCTION_TABLE_EX {
    RIO_EXTENSION_FUNCTION_TABLE table;
    bool loaded;
    RIO_EXTENSION_FUNCTION_TABLE_EX() : loaded(false) {
        ZeroMemory(&table, sizeof(table));
    }
};

class RIONetwork {
public:
    static bool Init();
    static void Shutdown();

    static RIO_EXTENSION_FUNCTION_TABLE& Rio();
    static RIO_CQ CreateEventCQ(uint32_t size, HANDLE& outEventHandle);

private:
    static inline WSADATA s_wsa{};
    static inline RIO_EXTENSION_FUNCTION_TABLE_EX s_rio{};
    static inline HANDLE s_rioIocp = NULL;
    static inline OVERLAPPED* s_rioOv = nullptr;
    static inline std::atomic<bool> s_inited{ false };
};
