// English: RIO AsyncIOProvider + RIOBufferPool test suite.
//          No GTest dependency - uses std::cout (same pattern as AsyncIOTest.cpp).
//          On systems without RIO support, tests are SKIP'd gracefully.
// 한글: RIO AsyncIOProvider + RIOBufferPool 테스트.
//       GTest 미사용, std::cout 기반 (AsyncIOTest.cpp 패턴).
//       RIO 미지원 환경에서는 SKIP 처리.

#ifdef _WIN32
#include "Network/Core/AsyncIOProvider.h"
#include "Platforms/Windows/RIOAsyncIOProvider.h"
#include "Core/Memory/RIOBufferPool.h"
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
    ::Core::Memory::RIOBufferPool pool;
    // Initialize(poolSize, slotSize) — pool loads RIO fn ptrs itself.
    // Initialize(poolSize, slotSize) — 풀이 직접 RIO 함수 포인터를 로드.
    if (!pool.Initialize(8, 65536))
    {
        std::cout << "[SKIP] " << name << " - RIO not available\n";
        return;
    }
    if (pool.PoolSize() == 8 && pool.FreeCount() == 8)
        Pass(name);
    else
        Fail(name, "Pool init failed or wrong counts");
    pool.Shutdown();
}

// -----------------------------------------------------------------------
void TestRIOBufferPoolAcquireRelease()
{
    const char *name = "RIOBufferPoolAcquireRelease";
    ::Core::Memory::RIOBufferPool pool;
    if (!pool.Initialize(4, 65536))
    {
        std::cout << "[SKIP] " << name << " - RIO not available\n";
        return;
    }

    auto slot1 = pool.Acquire();
    auto slot2 = pool.Acquire();

    if (!slot1.ptr || !slot2.ptr || pool.FreeCount() != 2)
    {
        Fail(name, "Acquire returned wrong state");
    }
    else
    {
        pool.Release(slot1.index);
        if (pool.FreeCount() == 3)
            Pass(name);
        else
            Fail(name, "Release did not restore availability");
    }

    pool.Shutdown();
}

// -----------------------------------------------------------------------
void TestRIOBufferPoolExhaustion()
{
    const char *name = "RIOBufferPoolExhaustion";
    ::Core::Memory::RIOBufferPool pool;
    if (!pool.Initialize(2, 4096))
    {
        std::cout << "[SKIP] " << name << " - RIO not available\n";
        return;
    }

    auto s1 = pool.Acquire();
    auto s2 = pool.Acquire();
    auto s3 = pool.Acquire(); // English: must return {nullptr,...} / 한글: nullptr 반환 필수
    (void)s1; (void)s2;

    if (s3.ptr == nullptr)
        Pass(name);
    else
        Fail(name, "Expected nullptr on pool exhaustion");

    pool.Shutdown();
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
