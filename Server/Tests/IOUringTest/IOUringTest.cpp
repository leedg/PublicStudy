// English: io_uring AsyncIOProvider + IOUringBufferPool test suite.
//          No GTest dependency - uses std::cout (same pattern as AsyncIOTest.cpp).
//          Compiled and run only on Linux with HAVE_LIBURING defined.
// 한글: io_uring AsyncIOProvider + IOUringBufferPool 테스트.
//       GTest 미사용, std::cout 기반 (AsyncIOTest.cpp 패턴).
//       Linux + HAVE_LIBURING 환경에서만 컴파일 및 실행.

#if defined(__linux__) && defined(HAVE_LIBURING)
#include "Network/Core/AsyncIOProvider.h"
#include "Platforms/Linux/IOUringAsyncIOProvider.h"
#include "Platforms/Linux/IOUringBufferPool.h"
#include <iostream>

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
void TestIOUringProviderInit()
{
    const char *name = "IOUringProviderInit";
    IOUringAsyncIOProvider provider;
    auto err = provider.Initialize(256, 128);
    // English: PlatformNotSupported is acceptable on kernels without io_uring.
    // 한글: io_uring 미지원 커널에서 PlatformNotSupported는 정상.
    if (err == AsyncIOError::Success ||
        err == AsyncIOError::PlatformNotSupported)
        Pass(name);
    else
        Fail(name, provider.GetLastError());
    if (provider.IsInitialized())
        provider.Shutdown();
}

// -----------------------------------------------------------------------
void TestIOUringBufferPoolInit()
{
    const char *name = "IOUringBufferPoolInit";
    IOUringAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success)
    {
        std::cout << "[SKIP] " << name << " - io_uring not available\n";
        return;
    }
    IOUringBufferPool pool;
    bool ok = pool.Initialize(&provider, 65536, 8);
    if (ok && pool.GetPoolSize() == 8 && pool.GetAvailable() == 8)
        Pass(name);
    else
        Fail(name, "Pool init failed or wrong counts");
    // English: Pool must shut down before provider to allow clean buffer deregistration.
    // 한글: 버퍼 해제 등록을 위해 pool을 provider보다 먼저 종료해야 함.
    pool.Shutdown();
    provider.Shutdown();
}

// -----------------------------------------------------------------------
void TestIOUringBufferPoolAcquireRelease()
{
    const char *name = "IOUringBufferPoolAcquireRelease";
    IOUringAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success)
    {
        std::cout << "[SKIP] " << name << " - io_uring not available\n";
        return;
    }
    IOUringBufferPool pool;
    if (!pool.Initialize(&provider, 65536, 4))
    {
        Fail(name, "Init failed");
        provider.Shutdown();
        return;
    }

    int64_t id1 = -1, id2 = -1;
    uint8_t *buf1 = pool.Acquire(id1);
    uint8_t *buf2 = pool.Acquire(id2);

    if (!buf1 || !buf2 || id1 < 0 || id2 < 0 || pool.GetAvailable() != 2)
    {
        Fail(name, "Acquire returned wrong state");
    }
    else
    {
        pool.Release(id1);
        if (pool.GetAvailable() == 3)
            Pass(name);
        else
            Fail(name, "Release did not restore availability");
    }

    // English: Pool must shut down before provider to allow clean buffer deregistration.
    // 한글: 버퍼 해제 등록을 위해 pool을 provider보다 먼저 종료해야 함.
    pool.Shutdown();
    provider.Shutdown();
}

// -----------------------------------------------------------------------
void TestIOUringBufferPoolExhaustion()
{
    const char *name = "IOUringBufferPoolExhaustion";
    IOUringAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success)
    {
        std::cout << "[SKIP] " << name << " - io_uring not available\n";
        return;
    }
    IOUringBufferPool pool;
    if (!pool.Initialize(&provider, 4096, 2))
    {
        Fail(name, "Init failed");
        provider.Shutdown();
        return;
    }

    int64_t id1 = -1, id2 = -1, id3 = -1;
    pool.Acquire(id1);
    pool.Acquire(id2);
    uint8_t *buf3 = pool.Acquire(id3); // English: must return nullptr / 한글: nullptr 반환 필수

    if (buf3 == nullptr && id3 == -1)
        Pass(name);
    else
        Fail(name, "Expected nullptr on pool exhaustion");

    // English: Pool must shut down before provider to allow clean buffer deregistration.
    // 한글: 버퍼 해제 등록을 위해 pool을 provider보다 먼저 종료해야 함.
    pool.Shutdown();
    provider.Shutdown();
}

// -----------------------------------------------------------------------
int main()
{
    std::cout << "=== io_uring AsyncIOProvider + BufferPool Tests ===\n\n";
    TestIOUringProviderInit();
    TestIOUringBufferPoolInit();
    TestIOUringBufferPoolAcquireRelease();
    TestIOUringBufferPoolExhaustion();
    std::cout << "\nResult: " << gPassed << " passed, " << gFailed
              << " failed\n";
    return gFailed > 0 ? 1 : 0;
}

#else // !(linux && liburing)
#include <iostream>
int main()
{
    std::cout << "[SKIP] IOUringTest: Linux + liburing only\n";
    return 0;
}
#endif // defined(__linux__) && defined(HAVE_LIBURING)
