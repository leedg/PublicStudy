// English: epoll AsyncIOProvider test suite.
//          No GTest dependency - uses std::cout (same pattern as IOUringTest.cpp).
//          Compiled and run only on Linux (epoll is standard; no extra library needed).
// 한글: epoll AsyncIOProvider 테스트.
//       GTest 미사용, std::cout 기반 (IOUringTest.cpp 패턴).
//       Linux 환경에서만 컴파일 및 실행 (epoll은 표준; 별도 라이브러리 불필요).

#ifdef __linux__
#include "Network/Core/AsyncIOProvider.h"
#include "Platforms/Linux/EpollAsyncIOProvider.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

using namespace Network::AsyncIO;
using namespace Network::AsyncIO::Linux;

static int gPassed = 0, gFailed = 0;

static void Pass(const char *name)
{
    std::cout << "[PASS] " << name << "\n";
    ++gPassed;
}

static void Fail(const char *name, const char *reason)
{
    std::cout << "[FAIL] " << name << " - " << reason << "\n";
    ++gFailed;
}

// -----------------------------------------------------------------------
void TestEpollProviderInit()
{
    const char *name = "EpollProviderInit";
    EpollAsyncIOProvider provider;
    auto err = provider.Initialize(256, 128);
    if (err == AsyncIOError::Success && provider.IsInitialized())
        Pass(name);
    else
        Fail(name, provider.GetLastError());
    if (provider.IsInitialized())
        provider.Shutdown();
}

// -----------------------------------------------------------------------
void TestEpollAssociateSocket()
{
    const char *name = "EpollAssociateSocket";
    EpollAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success)
    {
        Fail(name, "Provider init failed");
        return;
    }

    // English: Use a connected socketpair so AssociateSocket has a real fd.
    // 한글: 실제 fd를 갖는 연결된 socketpair 사용.
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) < 0)
    {
        Fail(name, "socketpair failed");
        provider.Shutdown();
        return;
    }

    auto err = provider.AssociateSocket(fds[1], nullptr);
    if (err == AsyncIOError::Success)
        Pass(name);
    else
        Fail(name, provider.GetLastError());

    close(fds[0]);
    close(fds[1]);
    provider.Shutdown();
}

// -----------------------------------------------------------------------
void TestEpollSendRecv()
{
    const char *name = "EpollSendRecv";
    EpollAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success)
    {
        Fail(name, "Provider init failed");
        return;
    }

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) < 0)
    {
        Fail(name, "socketpair failed");
        provider.Shutdown();
        return;
    }

    // English: fds[1] = receiver side registered with epoll.
    // 한글: fds[1] = epoll에 등록된 수신 측.
    if (provider.AssociateSocket(fds[1], nullptr) != AsyncIOError::Success)
    {
        Fail(name, "AssociateSocket failed");
        close(fds[0]);
        close(fds[1]);
        provider.Shutdown();
        return;
    }

    uint8_t recvBuf[64] = {};
    if (provider.RecvAsync(fds[1], recvBuf, sizeof(recvBuf), nullptr)
        != AsyncIOError::Success)
    {
        Fail(name, "RecvAsync failed");
        close(fds[0]);
        close(fds[1]);
        provider.Shutdown();
        return;
    }

    // English: Write to fds[0] to trigger EPOLLIN on fds[1].
    // 한글: fds[0]에 쓰기하여 fds[1]에 EPOLLIN 이벤트 발생.
    const char *msg = "hello";
    ssize_t written = write(fds[0], msg, 5);
    if (written != 5)
    {
        Fail(name, "write to socket pair failed");
        close(fds[0]);
        close(fds[1]);
        provider.Shutdown();
        return;
    }

    CompletionEntry entries[8] = {};
    int n = provider.ProcessCompletions(entries, 8, 200 /*ms*/);

    if (n >= 1 && entries[0].mBytesTransferred == 5 &&
        std::memcmp(recvBuf, msg, 5) == 0)
        Pass(name);
    else
        Fail(name, "ProcessCompletions did not return expected completion");

    close(fds[0]);
    close(fds[1]);
    provider.Shutdown();
}

// -----------------------------------------------------------------------
void TestEpollBufferRegistration()
{
    // English: epoll does not support pre-registered buffers; verify the
    //          API returns the expected sentinel values (not a hard failure).
    // 한글: epoll은 사전 등록 버퍼를 지원하지 않음; API가 예상된
    //       센티넬 값을 반환하는지 검증 (하드 실패 아님).
    const char *name = "EpollBufferRegistration";
    EpollAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success)
    {
        Fail(name, "Provider init failed");
        return;
    }

    uint8_t buf[64];
    int64_t id = provider.RegisterBuffer(buf, sizeof(buf));
    auto err   = provider.UnregisterBuffer(id);

    if (id == -1 && err == AsyncIOError::PlatformNotSupported)
        Pass(name);
    else
        Fail(name, "Expected id=-1 and PlatformNotSupported");

    provider.Shutdown();
}

// -----------------------------------------------------------------------
int main()
{
    std::cout << "=== epoll AsyncIOProvider Tests ===\n\n";
    TestEpollProviderInit();
    TestEpollAssociateSocket();
    TestEpollSendRecv();
    TestEpollBufferRegistration();
    std::cout << "\nResult: " << gPassed << " passed, " << gFailed
              << " failed\n";
    return gFailed > 0 ? 1 : 0;
}

#else // !__linux__
#include <iostream>
int main()
{
    std::cout << "[SKIP] EpollTest: Linux only\n";
    return 0;
}
#endif // __linux__
