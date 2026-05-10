#if defined(__linux__) && defined(HAVE_LIBURING)

#include "Core/Memory/IOUringBufferPool.h"
#include "Network/Core/AsyncIOProvider.h"
#include "Platforms/Linux/IOUringAsyncIOProvider.h"

#include <iostream>

using namespace Network::AsyncIO;
using namespace Network::AsyncIO::Linux;
using Network::Core::Memory::BufferSlot;
using Network::Core::Memory::IOUringBufferPool;

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

static void TestIOUringProviderInit()
{
    const char* name = "IOUringProviderInit";

    IOUringAsyncIOProvider provider;
    const auto err = provider.Initialize(256, 128);
    if (err == AsyncIOError::Success || err == AsyncIOError::PlatformNotSupported) {
        Pass(name);
    } else {
        Fail(name, provider.GetLastError());
    }

    provider.Shutdown();
}

static void TestIOUringBufferPoolInit()
{
    const char* name = "IOUringBufferPoolInit";

    IOUringBufferPool pool;
    const bool ok = pool.Initialize(8, 65536);
    if (ok &&
        pool.PoolSize() == 8 &&
        pool.SlotSize() == 65536 &&
        pool.FreeCount() == 8 &&
        !pool.IsFixedBufferMode()) {
        Pass(name);
    } else {
        Fail(name, "pool init produced unexpected state");
    }

    pool.Shutdown();
}

static void TestIOUringBufferPoolAcquireRelease()
{
    const char* name = "IOUringBufferPoolAcquireRelease";

    IOUringBufferPool pool;
    if (!pool.Initialize(4, 65536)) {
        Fail(name, "Init failed");
        return;
    }

    const BufferSlot slot1 = pool.Acquire();
    const BufferSlot slot2 = pool.Acquire();

    if (slot1.ptr == nullptr ||
        slot2.ptr == nullptr ||
        slot1.index == slot2.index ||
        pool.FreeCount() != 2) {
        Fail(name, "Acquire returned invalid slots");
        pool.Shutdown();
        return;
    }

    pool.Release(slot1.index);
    const bool firstReleaseOk = (pool.FreeCount() == 3);
    pool.Release(slot2.index);

    if (firstReleaseOk && pool.FreeCount() == 4) {
        Pass(name);
    } else {
        Fail(name, "Release did not restore free count");
    }

    pool.Shutdown();
}

static void TestIOUringBufferPoolExhaustion()
{
    const char* name = "IOUringBufferPoolExhaustion";

    IOUringBufferPool pool;
    if (!pool.Initialize(2, 4096)) {
        Fail(name, "Init failed");
        return;
    }

    const BufferSlot slot1 = pool.Acquire();
    const BufferSlot slot2 = pool.Acquire();
    const BufferSlot slot3 = pool.Acquire();

    if (slot1.ptr != nullptr &&
        slot2.ptr != nullptr &&
        slot3.ptr == nullptr &&
        pool.FreeCount() == 0) {
        Pass(name);
    } else {
        Fail(name, "Expected empty slot on exhaustion");
    }

    if (slot1.ptr != nullptr) {
        pool.Release(slot1.index);
    }
    if (slot2.ptr != nullptr) {
        pool.Release(slot2.index);
    }

    pool.Shutdown();
}

int main()
{
    std::cout << "=== io_uring AsyncIOProvider + BufferPool Tests ===\n\n";

    TestIOUringProviderInit();
    TestIOUringBufferPoolInit();
    TestIOUringBufferPoolAcquireRelease();
    TestIOUringBufferPoolExhaustion();

    std::cout << "\nResult: " << gPassed << " passed, " << gFailed << " failed\n";
    return gFailed > 0 ? 1 : 0;
}

#else

#include <iostream>

int main()
{
    std::cout << "[SKIP] IOUringTest: Linux + liburing only\n";
    return 0;
}

#endif
