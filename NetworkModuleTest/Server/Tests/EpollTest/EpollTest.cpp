#ifdef __linux__

#include "Network/Core/AsyncIOProvider.h"
#include "Platforms/Linux/EpollAsyncIOProvider.h"

#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

using namespace Network::AsyncIO;
using namespace Network::AsyncIO::Linux;

static int gPassed = 0;
static int gFailed = 0;

static void Pass(const char* name)
{
    std::cout << "[PASS] " << name << "\n";
    ++gPassed;
}

static void Fail(const char* name, const char* reason)
{
    std::cout << "[FAIL] " << name << " - " << reason << "\n";
    ++gFailed;
}

static void ClosePair(int (&fds)[2])
{
    if (fds[0] >= 0) {
        close(fds[0]);
        fds[0] = -1;
    }
    if (fds[1] >= 0) {
        close(fds[1]);
        fds[1] = -1;
    }
}

static void TestEpollProviderInit()
{
    const char* name = "EpollProviderInit";

    EpollAsyncIOProvider provider;
    const auto err = provider.Initialize(256, 128);
    if (err == AsyncIOError::Success && provider.IsInitialized()) {
        Pass(name);
    } else {
        Fail(name, provider.GetLastError());
    }

    provider.Shutdown();
}

static void TestEpollAssociateSocket()
{
    const char* name = "EpollAssociateSocket";

    EpollAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success) {
        Fail(name, "Provider init failed");
        return;
    }

    int fds[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) < 0) {
        Fail(name, "socketpair failed");
        provider.Shutdown();
        return;
    }

    const auto err = provider.AssociateSocket(fds[1], 0);
    if (err == AsyncIOError::Success) {
        Pass(name);
    } else {
        Fail(name, provider.GetLastError());
    }

    ClosePair(fds);
    provider.Shutdown();
}

static void TestEpollSendRecv()
{
    const char* name = "EpollSendRecv";
    constexpr RequestContext recvContext = 0x1234;

    EpollAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success) {
        Fail(name, "Provider init failed");
        return;
    }

    int fds[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) < 0) {
        Fail(name, "socketpair failed");
        provider.Shutdown();
        return;
    }

    if (provider.AssociateSocket(fds[1], 0) != AsyncIOError::Success) {
        Fail(name, "AssociateSocket failed");
        ClosePair(fds);
        provider.Shutdown();
        return;
    }

    char recvBuffer[64] = {};
    if (provider.RecvAsync(fds[1], recvBuffer, sizeof(recvBuffer), recvContext) != AsyncIOError::Success) {
        Fail(name, "RecvAsync failed");
        ClosePair(fds);
        provider.Shutdown();
        return;
    }

    static const char message[] = "hello";
    if (write(fds[0], message, 5) != 5) {
        Fail(name, "socket write failed");
        ClosePair(fds);
        provider.Shutdown();
        return;
    }

    CompletionEntry entries[8] = {};
    const int completed = provider.ProcessCompletions(entries, 8, 200);

    if (completed >= 1 &&
        entries[0].mContext == recvContext &&
        entries[0].mType == AsyncIOType::Recv &&
        entries[0].mResult == 5 &&
        entries[0].mOsError == 0 &&
        std::memcmp(recvBuffer, message, 5) == 0) {
        Pass(name);
    } else {
        Fail(name, "unexpected completion payload");
    }

    ClosePair(fds);
    provider.Shutdown();
}

static void TestEpollBufferRegistration()
{
    const char* name = "EpollBufferRegistration";

    EpollAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success) {
        Fail(name, "Provider init failed");
        return;
    }

    char buffer[64] = {};
    const int64_t bufferId = provider.RegisterBuffer(buffer, sizeof(buffer));
    const auto err = provider.UnregisterBuffer(bufferId);

    if (bufferId == -1 && err == AsyncIOError::PlatformNotSupported) {
        Pass(name);
    } else {
        Fail(name, "expected no-op epoll buffer registration");
    }

    provider.Shutdown();
}

int main()
{
    std::cout << "=== epoll AsyncIOProvider Tests ===\n\n";

    TestEpollProviderInit();
    TestEpollAssociateSocket();
    TestEpollSendRecv();
    TestEpollBufferRegistration();

    std::cout << "\nResult: " << gPassed << " passed, " << gFailed << " failed\n";
    return gFailed > 0 ? 1 : 0;
}

#else

#include <iostream>

int main()
{
    std::cout << "[SKIP] EpollTest: Linux only\n";
    return 0;
}

#endif
