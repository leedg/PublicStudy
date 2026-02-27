// English: RIO AsyncIOProvider + RIOBufferPool test suite.
//          No GTest dependency - uses std::cout (same pattern as AsyncIOTest.cpp).
//          On systems without RIO support, tests are SKIP'd gracefully.
// 한글: RIO AsyncIOProvider + RIOBufferPool 테스트.
//       GTest 미사용, std::cout 기반 (AsyncIOTest.cpp 패턴).
//       RIO 미지원 환경에서는 SKIP 처리.

#ifdef _WIN32
#include "Network/Core/AsyncIOProvider.h"
#include "Platforms/Windows/RIOAsyncIOProvider.h"
#include "Platforms/Windows/RIOBufferPool.h"
#include <iostream>

using namespace Network::AsyncIO;
using namespace Network::AsyncIO::Windows;

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
void TestRIOProviderInit()
{
    const char *name = "RIOProviderInit";
    RIOAsyncIOProvider provider;
    auto err = provider.Initialize(256, 128);
    // English: PlatformNotSupported is acceptable on pre-Win8 machines.
    // 한글: Win8 미만 환경에서 PlatformNotSupported는 정상.
    if (err == AsyncIOError::Success ||
        err == AsyncIOError::PlatformNotSupported)
        Pass(name);
    else
        Fail(name, provider.GetLastError());
    if (provider.IsInitialized())
        provider.Shutdown();
}

// -----------------------------------------------------------------------
void TestRIOBufferPoolInit()
{
    const char *name = "RIOBufferPoolInit";
    RIOAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success)
    {
        std::cout << "[SKIP] " << name << " - RIO not available\n";
        return;
    }
    RIOBufferPool pool;
    bool ok = pool.Initialize(&provider, 65536, 8);
    if (ok && pool.GetPoolSize() == 8 && pool.GetAvailable() == 8)
        Pass(name);
    else
        Fail(name, "Pool init failed or wrong counts");
    pool.Shutdown();
    provider.Shutdown();
}

// -----------------------------------------------------------------------
void TestRIOBufferPoolAcquireRelease()
{
    const char *name = "RIOBufferPoolAcquireRelease";
    RIOAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success)
    {
        std::cout << "[SKIP] " << name << " - RIO not available\n";
        return;
    }
    RIOBufferPool pool;
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

    pool.Shutdown();
    provider.Shutdown();
}

// -----------------------------------------------------------------------
void TestRIOBufferPoolExhaustion()
{
    const char *name = "RIOBufferPoolExhaustion";
    RIOAsyncIOProvider provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success)
    {
        std::cout << "[SKIP] " << name << " - RIO not available\n";
        return;
    }
    RIOBufferPool pool;
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

    pool.Shutdown();
    provider.Shutdown();
}

// -----------------------------------------------------------------------
int main()
{
    std::cout << "=== RIO AsyncIOProvider + BufferPool Tests ===\n\n";
    TestRIOProviderInit();
    TestRIOBufferPoolInit();
    TestRIOBufferPoolAcquireRelease();
    TestRIOBufferPoolExhaustion();
    std::cout << "\nResult: " << gPassed << " passed, " << gFailed
              << " failed\n";
    return gFailed > 0 ? 1 : 0;
}

#else // !_WIN32
#include <iostream>
int main()
{
    std::cout << "[SKIP] RIOTest: Windows only\n";
    return 0;
}
#endif // _WIN32
