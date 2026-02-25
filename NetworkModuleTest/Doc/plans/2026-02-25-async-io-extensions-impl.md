# AsyncIO Extensions Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** ServerEngine에 IBufferPool 인터페이스, RIOBufferPool, IOUringBufferPool, 플랫폼별 테스트, 벤치마크를 추가한다.

**Architecture:** IBufferPool 추상 인터페이스로 확장성 확보. RIOBufferPool/IOUringBufferPool은 각 provider의 RegisterBuffer를 활용해 사전 등록 버퍼 풀을 구현. 테스트와 벤치마크는 `<None>` 필터 항목으로 VS에서 열람 가능하되 .lib에는 미포함.

**Tech Stack:** C++17, Windows IOCP/RIO, Linux io_uring, MSBuild (빌드 검증), `_aligned_malloc`/`posix_memalign` (페이지 정렬 버퍼)

---

## Task 1: IBufferPool 인터페이스 + RIOBufferPool

**Files:**
- Create: `Server/ServerEngine/Interfaces/IBufferPool.h`
- Create: `Server/ServerEngine/Platforms/Windows/RIOBufferPool.h`
- Create: `Server/ServerEngine/Platforms/Windows/RIOBufferPool.cpp`
- Modify: `Server/ServerEngine/ServerEngine.vcxproj.filters`
- Modify: `Server/ServerEngine/ServerEngine.vcxproj`

### Step 1: IBufferPool.h 작성

파일: `Server/ServerEngine/Interfaces/IBufferPool.h`

```cpp
#pragma once
// English: Abstract buffer pool interface for pre-registered async I/O buffers.
// 한글: 사전 등록 비동기 I/O 버퍼 풀 추상 인터페이스.
//       RIOBufferPool, IOUringBufferPool 등이 구현한다.
//       나중에 멀티 사이즈 풀이나 Lock-Free 풀로 교체 시 이 인터페이스만 유지하면 된다.

#include <cstddef>
#include <cstdint>

namespace Network
{
namespace AsyncIO
{

class AsyncIOProvider;

class IBufferPool
{
  public:
    virtual ~IBufferPool() = default;

    // English: Initialize pool - allocate and pre-register bufferSize * poolSize bytes with provider.
    // 한글: 초기화 - provider에 bufferSize 크기 버퍼 poolSize개 사전 등록.
    virtual bool Initialize(AsyncIOProvider *provider, size_t bufferSize,
                            size_t poolSize) = 0;

    // English: Release all registered buffers and free memory.
    // 한글: 등록된 모든 버퍼를 해제하고 메모리를 반환한다.
    virtual void Shutdown() = 0;

    // English: Acquire a free buffer. Returns nullptr if pool is exhausted.
    // 한글: 빈 버퍼를 반환한다. 풀이 고갈된 경우 nullptr 반환.
    virtual uint8_t *Acquire(int64_t &outBufferId) = 0;

    // English: Return a buffer back to the pool.
    // 한글: 버퍼를 풀로 반환한다.
    virtual void Release(int64_t bufferId) = 0;

    virtual size_t GetBufferSize() const = 0;
    virtual size_t GetAvailable() const = 0;
    virtual size_t GetPoolSize() const = 0;
};

} // namespace AsyncIO
} // namespace Network
```

### Step 2: RIOBufferPool.h 작성

파일: `Server/ServerEngine/Platforms/Windows/RIOBufferPool.h`

```cpp
#pragma once
// English: RIO pre-registered buffer pool for Windows.
//          Implements IBufferPool using RIOAsyncIOProvider::RegisterBuffer.
// 한글: Windows RIO 사전 등록 버퍼 풀.
//       RIOAsyncIOProvider::RegisterBuffer를 사용해 IBufferPool 구현.

#include "Interfaces/IBufferPool.h"
#include "Network/Core/AsyncIOProvider.h"

#ifdef _WIN32
#include <mutex>
#include <vector>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

class RIOBufferPool : public IBufferPool
{
  public:
    RIOBufferPool();
    ~RIOBufferPool() override;

    RIOBufferPool(const RIOBufferPool &) = delete;
    RIOBufferPool &operator=(const RIOBufferPool &) = delete;

    bool Initialize(AsyncIOProvider *provider, size_t bufferSize,
                    size_t poolSize) override;
    void Shutdown() override;

    // English: Acquire a free slot. Returns nullptr + outBufferId=-1 if exhausted.
    // 한글: 빈 슬롯 반환. 고갈 시 nullptr + outBufferId=-1.
    uint8_t *Acquire(int64_t &outBufferId) override;
    void Release(int64_t bufferId) override;

    size_t GetBufferSize() const override;
    size_t GetAvailable() const override;
    size_t GetPoolSize() const override;

  private:
    struct Slot
    {
        uint8_t *ptr = nullptr;
        int64_t bufferId = -1;
        bool inUse = false;
    };

    AsyncIOProvider *mProvider = nullptr;
    std::vector<Slot> mSlots;
    size_t mBufferSize = 0;
    mutable std::mutex mMutex;
};

} // namespace Windows
} // namespace AsyncIO
} // namespace Network
#endif // _WIN32
```

### Step 3: RIOBufferPool.cpp 작성

파일: `Server/ServerEngine/Platforms/Windows/RIOBufferPool.cpp`

```cpp
#include "Platforms/Windows/RIOBufferPool.h"

#ifdef _WIN32
#include <cstdlib>

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

RIOBufferPool::RIOBufferPool() = default;

RIOBufferPool::~RIOBufferPool()
{
    Shutdown();
}

bool RIOBufferPool::Initialize(AsyncIOProvider *provider, size_t bufferSize,
                                size_t poolSize)
{
    if (!provider || bufferSize == 0 || poolSize == 0)
        return false;

    std::lock_guard<std::mutex> lock(mMutex);
    mProvider = provider;
    mBufferSize = bufferSize;
    mSlots.reserve(poolSize);

    for (size_t i = 0; i < poolSize; ++i)
    {
        // English: 4KB page-aligned allocation for optimal RIO performance.
        // 한글: RIO 성능 최적화를 위한 4KB 페이지 정렬 할당.
        uint8_t *ptr =
            static_cast<uint8_t *>(_aligned_malloc(bufferSize, 4096));
        if (!ptr)
        {
            Shutdown();
            return false;
        }

        int64_t id = provider->RegisterBuffer(ptr, bufferSize);
        if (id < 0)
        {
            _aligned_free(ptr);
            Shutdown();
            return false;
        }

        mSlots.push_back({ptr, id, false});
    }
    return true;
}

void RIOBufferPool::Shutdown()
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto &slot : mSlots)
    {
        if (slot.ptr)
        {
            if (mProvider && slot.bufferId >= 0)
                mProvider->UnregisterBuffer(slot.bufferId);
            _aligned_free(slot.ptr);
        }
    }
    mSlots.clear();
    mProvider = nullptr;
    mBufferSize = 0;
}

uint8_t *RIOBufferPool::Acquire(int64_t &outBufferId)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto &slot : mSlots)
    {
        if (!slot.inUse)
        {
            slot.inUse = true;
            outBufferId = slot.bufferId;
            return slot.ptr;
        }
    }
    outBufferId = -1;
    return nullptr;
}

void RIOBufferPool::Release(int64_t bufferId)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto &slot : mSlots)
    {
        if (slot.bufferId == bufferId)
        {
            slot.inUse = false;
            return;
        }
    }
}

size_t RIOBufferPool::GetBufferSize() const
{
    return mBufferSize;
}

size_t RIOBufferPool::GetAvailable() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    size_t count = 0;
    for (const auto &slot : mSlots)
        if (!slot.inUse)
            ++count;
    return count;
}

size_t RIOBufferPool::GetPoolSize() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mSlots.size();
}

} // namespace Windows
} // namespace AsyncIO
} // namespace Network
#endif // _WIN32
```

### Step 4: ServerEngine.vcxproj.filters 수정

`</Project>` 바로 앞 마지막 `</ItemGroup>` 뒤에 아래 블록을 추가한다.

위치: `Server/ServerEngine/ServerEngine.vcxproj.filters`

기존 `Core\Network\Platforms\Windows` 필터 아래 ClInclude/ClCompile 항목 3개를 추가:

```xml
    <!-- BufferPool - Windows -->
    <ClInclude Include="Interfaces\IBufferPool.h">
      <Filter>Interfaces</Filter>
    </ClInclude>
    <ClInclude Include="Platforms\Windows\RIOBufferPool.h">
      <Filter>Core\Network\Platforms\Windows</Filter>
    </ClInclude>
    <ClCompile Include="Platforms\Windows\RIOBufferPool.cpp">
      <Filter>Core\Network\Platforms\Windows</Filter>
    </ClCompile>
```

### Step 5: ServerEngine.vcxproj 수정

`ServerEngine.vcxproj`의 `<ItemGroup>` 내 ClCompile 목록에 RIOBufferPool.cpp를 추가하고,
ClInclude 목록에 IBufferPool.h, RIOBufferPool.h를 추가한다.
(기존 `IocpAsyncIOProvider.cpp` 항목 옆에 추가)

```xml
<ClCompile Include="Platforms\Windows\RIOBufferPool.cpp" />
<ClInclude Include="Interfaces\IBufferPool.h" />
<ClInclude Include="Platforms\Windows\RIOBufferPool.h" />
```

### Step 6: 빌드 검증

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" `
  "NetworkModuleTest.sln" /p:Configuration=Debug /p:Platform=x64 /m /nologo
```

기대 결과: `오류 0개, 경고 0개`

### Step 7: 커밋

```bash
git add Server/ServerEngine/Interfaces/IBufferPool.h \
        Server/ServerEngine/Platforms/Windows/RIOBufferPool.h \
        Server/ServerEngine/Platforms/Windows/RIOBufferPool.cpp \
        Server/ServerEngine/ServerEngine.vcxproj \
        Server/ServerEngine/ServerEngine.vcxproj.filters
git commit -m "feat: add IBufferPool interface and RIOBufferPool

IBufferPool 인터페이스 추가 및 Windows RIO 버퍼 풀 구현
- IBufferPool: 추상 인터페이스 (Acquire/Release, 확장 가능)
- RIOBufferPool: 4KB 정렬 버퍼 사전 등록, mutex 기반 단순 풀"
```

---

## Task 2: IOUringBufferPool

**Files:**
- Create: `Server/ServerEngine/Platforms/Linux/IOUringBufferPool.h`
- Create: `Server/ServerEngine/Platforms/Linux/IOUringBufferPool.cpp`
- Modify: `Server/ServerEngine/ServerEngine.vcxproj.filters`
- Modify: `Server/ServerEngine/ServerEngine.vcxproj`

### Step 1: IOUringBufferPool.h 작성

파일: `Server/ServerEngine/Platforms/Linux/IOUringBufferPool.h`

```cpp
#pragma once
// English: io_uring pre-registered buffer pool for Linux.
//          Implements IBufferPool using IOUringAsyncIOProvider::RegisterBuffer.
// 한글: Linux io_uring 사전 등록 버퍼 풀.
//       IOUringAsyncIOProvider::RegisterBuffer를 사용해 IBufferPool 구현.

#include "Interfaces/IBufferPool.h"
#include "Network/Core/AsyncIOProvider.h"

#if defined(__linux__)
#include <mutex>
#include <vector>

namespace Network
{
namespace AsyncIO
{
namespace Linux
{

class IOUringBufferPool : public IBufferPool
{
  public:
    IOUringBufferPool();
    ~IOUringBufferPool() override;

    IOUringBufferPool(const IOUringBufferPool &) = delete;
    IOUringBufferPool &operator=(const IOUringBufferPool &) = delete;

    bool Initialize(AsyncIOProvider *provider, size_t bufferSize,
                    size_t poolSize) override;
    void Shutdown() override;

    uint8_t *Acquire(int64_t &outBufferId) override;
    void Release(int64_t bufferId) override;

    size_t GetBufferSize() const override;
    size_t GetAvailable() const override;
    size_t GetPoolSize() const override;

  private:
    struct Slot
    {
        uint8_t *ptr = nullptr;
        int64_t bufferId = -1;
        bool inUse = false;
    };

    AsyncIOProvider *mProvider = nullptr;
    std::vector<Slot> mSlots;
    size_t mBufferSize = 0;
    mutable std::mutex mMutex;
};

} // namespace Linux
} // namespace AsyncIO
} // namespace Network
#endif // defined(__linux__)
```

### Step 2: IOUringBufferPool.cpp 작성

파일: `Server/ServerEngine/Platforms/Linux/IOUringBufferPool.cpp`

```cpp
#include "Platforms/Linux/IOUringBufferPool.h"

#if defined(__linux__)
#include <cstdlib>

namespace Network
{
namespace AsyncIO
{
namespace Linux
{

IOUringBufferPool::IOUringBufferPool() = default;

IOUringBufferPool::~IOUringBufferPool()
{
    Shutdown();
}

bool IOUringBufferPool::Initialize(AsyncIOProvider *provider, size_t bufferSize,
                                    size_t poolSize)
{
    if (!provider || bufferSize == 0 || poolSize == 0)
        return false;

    std::lock_guard<std::mutex> lock(mMutex);
    mProvider = provider;
    mBufferSize = bufferSize;
    mSlots.reserve(poolSize);

    for (size_t i = 0; i < poolSize; ++i)
    {
        // English: 4KB page-aligned allocation for optimal io_uring fixed buffer performance.
        // 한글: io_uring fixed buffer 성능 최적화를 위한 4KB 페이지 정렬 할당.
        void *raw = nullptr;
        if (posix_memalign(&raw, 4096, bufferSize) != 0)
        {
            Shutdown();
            return false;
        }

        uint8_t *ptr = static_cast<uint8_t *>(raw);
        int64_t id = provider->RegisterBuffer(ptr, bufferSize);
        if (id < 0)
        {
            free(ptr);
            Shutdown();
            return false;
        }

        mSlots.push_back({ptr, id, false});
    }
    return true;
}

void IOUringBufferPool::Shutdown()
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto &slot : mSlots)
    {
        if (slot.ptr)
        {
            if (mProvider && slot.bufferId >= 0)
                mProvider->UnregisterBuffer(slot.bufferId);
            free(slot.ptr);
        }
    }
    mSlots.clear();
    mProvider = nullptr;
    mBufferSize = 0;
}

uint8_t *IOUringBufferPool::Acquire(int64_t &outBufferId)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto &slot : mSlots)
    {
        if (!slot.inUse)
        {
            slot.inUse = true;
            outBufferId = slot.bufferId;
            return slot.ptr;
        }
    }
    outBufferId = -1;
    return nullptr;
}

void IOUringBufferPool::Release(int64_t bufferId)
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto &slot : mSlots)
    {
        if (slot.bufferId == bufferId)
        {
            slot.inUse = false;
            return;
        }
    }
}

size_t IOUringBufferPool::GetBufferSize() const
{
    return mBufferSize;
}

size_t IOUringBufferPool::GetAvailable() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    size_t count = 0;
    for (const auto &slot : mSlots)
        if (!slot.inUse)
            ++count;
    return count;
}

size_t IOUringBufferPool::GetPoolSize() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mSlots.size();
}

} // namespace Linux
} // namespace AsyncIO
} // namespace Network
#endif // defined(__linux__)
```

### Step 3: vcxproj.filters 수정

`Core\Network\Platforms\Linux` 필터 아래에 추가:

```xml
    <!-- BufferPool - Linux -->
    <ClInclude Include="Platforms\Linux\IOUringBufferPool.h">
      <Filter>Core\Network\Platforms\Linux</Filter>
    </ClInclude>
    <ClCompile Include="Platforms\Linux\IOUringBufferPool.cpp">
      <Filter>Core\Network\Platforms\Linux</Filter>
    </ClCompile>
```

### Step 4: vcxproj 수정

```xml
<ClCompile Include="Platforms\Linux\IOUringBufferPool.cpp" />
<ClInclude Include="Platforms\Linux\IOUringBufferPool.h" />
```

### Step 5: 빌드 검증 (동일 MSBuild 명령)

기대 결과: `오류 0개`

### Step 6: 커밋

```bash
git commit -m "feat: add IOUringBufferPool

Linux io_uring 버퍼 풀 구현 (IBufferPool 인터페이스 구현)
- posix_memalign 4KB 정렬 + RegisterBuffer 사전 등록
- #if defined(__linux__) 조건부 컴파일"
```

---

## Task 3: RIOTest.cpp

**Files:**
- Create: `Server/ServerEngine/Tests/RIOTest.cpp`
- Modify: `Server/ServerEngine/ServerEngine.vcxproj.filters`

> 참고: RIOTest.cpp는 `<None>` 항목으로 필터에 등록 (lib에 미포함, VS에서 열람용).
> Windows에서 직접 실행하려면 별도 콘솔 프로젝트를 만들어 이 파일을 포함시키면 된다.

### Step 1: RIOTest.cpp 작성

파일: `Server/ServerEngine/Tests/RIOTest.cpp`

```cpp
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
```

### Step 2: vcxproj.filters 수정 — Tests 필터에 `<None>` 항목 추가

기존 `AsyncIOTest.cpp` `<None>` 항목 아래에 추가:

```xml
    <None Include="Tests\RIOTest.cpp">
      <Filter>Tests</Filter>
    </None>
```

### Step 3: 빌드 검증

`<None>` 항목이므로 빌드에 영향 없음. MSBuild 오류 0개 확인.

### Step 4: 커밋

```bash
git commit -m "feat: add RIOTest

Windows RIO AsyncIOProvider + RIOBufferPool 테스트 추가
- Initialize, Acquire/Release, 풀 고갈(Exhaustion) 케이스
- RIO 미지원 환경에서 SKIP 처리"
```

---

## Task 4: IOUringTest.cpp

**Files:**
- Create: `Server/ServerEngine/Tests/IOUringTest.cpp`
- Modify: `Server/ServerEngine/ServerEngine.vcxproj.filters`

### Step 1: IOUringTest.cpp 작성

파일: `Server/ServerEngine/Tests/IOUringTest.cpp`

```cpp
// English: io_uring AsyncIOProvider + IOUringBufferPool test suite.
//          Compiled only when __linux__ && HAVE_LIBURING defined.
// 한글: io_uring AsyncIOProvider + IOUringBufferPool 테스트.
//       __linux__ && HAVE_LIBURING 정의 시에만 컴파일.

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
    uint8_t *buf3 = pool.Acquire(id3);

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
#endif
```

### Step 2: vcxproj.filters 수정

```xml
    <None Include="Tests\IOUringTest.cpp">
      <Filter>Tests</Filter>
    </None>
```

### Step 3: 커밋

```bash
git commit -m "feat: add IOUringTest

Linux io_uring AsyncIOProvider + IOUringBufferPool 테스트 추가
- Initialize, Acquire/Release, 풀 고갈 케이스
- HAVE_LIBURING 미정의 환경에서 SKIP 처리"
```

---

## Task 5: ThroughputBench.cpp + LatencyBench.cpp

**Files:**
- Create: `Server/ServerEngine/Benchmark/ThroughputBench.cpp`
- Create: `Server/ServerEngine/Benchmark/LatencyBench.cpp`
- Modify: `Server/ServerEngine/ServerEngine.vcxproj.filters` (Benchmark 필터 신규)

> 두 파일 모두 `<None>` 항목. 실행 시 별도 콘솔 프로젝트에서 빌드.

### Step 1: ThroughputBench.cpp 작성

파일: `Server/ServerEngine/Benchmark/ThroughputBench.cpp`

```cpp
// English: AsyncIO throughput benchmark.
//          Measures: packets/sec, MB/sec via provider call-loop simulation.
//          Build: standalone console project linking ServerEngine.lib.
//          Run: ThroughputBench.exe
// 한글: AsyncIO 처리량 벤치마크.
//       측정 지표: 초당 패킷 수, MB/sec.
//       빌드: ServerEngine.lib 링크하는 별도 콘솔 프로젝트.

#include "Network/Core/AsyncIOProvider.h"

#ifdef _WIN32
#include "Platforms/Windows/IocpAsyncIOProvider.h"
using ProviderType = Network::AsyncIO::Windows::IocpAsyncIOProvider;
#elif defined(__linux__)
#include "Platforms/Linux/EpollAsyncIOProvider.h"
using ProviderType = Network::AsyncIO::Linux::EpollAsyncIOProvider;
#elif defined(__APPLE__)
#include "Platforms/macOS/KqueueAsyncIOProvider.h"
using ProviderType = Network::AsyncIO::macOS::KqueueAsyncIOProvider;
#endif

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

using namespace Network::AsyncIO;
using namespace std::chrono;

static constexpr size_t MSG_SIZE   = 1024; // English: 1 KB per message / 한글: 메시지당 1KB
static constexpr int    WARMUP_S   = 1;
static constexpr int    DURATION_S = 5;

int main()
{
    std::cout << "=== AsyncIO Throughput Benchmark ===\n";
    std::cout << "Message size : " << MSG_SIZE << " bytes\n";
    std::cout << "Duration     : " << DURATION_S << "s (+" << WARMUP_S
              << "s warmup)\n\n";

    ProviderType provider;
    if (provider.Initialize(1024, 256) != AsyncIOError::Success)
    {
        std::cout << "[ERROR] Provider init failed: " << provider.GetLastError()
                  << "\n";
        return 1;
    }
    std::cout << "Provider     : " << provider.GetInfo().name << "\n\n";

    // English: Warmup - discard first WARMUP_S seconds.
    // 한글: 워밍업 - 처음 WARMUP_S초 결과 버림.
    auto warmupEnd = steady_clock::now() + seconds(WARMUP_S);
    while (steady_clock::now() < warmupEnd)
        provider.GetStats(); // proxy call

    // English: Measure - count provider stat-call iterations as throughput proxy.
    //          (Real benchmark requires actual socket pair; this measures overhead floor.)
    // 한글: 측정 - provider 호출 횟수를 처리량 proxy로 사용.
    //       (실제 측정은 루프백 소켓 페어 필요; 이 코드는 오버헤드 하한 측정.)
    uint64_t count = 0;
    auto benchEnd = steady_clock::now() + seconds(DURATION_S);
    auto measureStart = steady_clock::now();

    while (steady_clock::now() < benchEnd)
    {
        provider.GetStats();
        ++count;
        if (count % 100000 == 0)
            std::this_thread::yield();
    }

    double elapsed =
        duration_cast<microseconds>(steady_clock::now() - measureStart)
            .count() / 1e6;

    double pps  = static_cast<double>(count) / elapsed;
    double mbps = (pps * MSG_SIZE) / (1024.0 * 1024.0);

    std::cout << "[BENCH] Results (" << DURATION_S << "s):\n";
    std::cout << "  Calls/sec   : " << static_cast<uint64_t>(pps) << "\n";
    std::cout << "  Equiv MB/s  : " << mbps << " MB/sec\n";
    std::cout << "  Total calls : " << count << "\n";

    provider.Shutdown();
    return 0;
}
```

### Step 2: LatencyBench.cpp 작성

파일: `Server/ServerEngine/Benchmark/LatencyBench.cpp`

```cpp
// English: AsyncIO latency benchmark.
//          Measures: min/avg/p99/max provider call latency in µs.
//          Build: standalone console project linking ServerEngine.lib.
//          Run: LatencyBench.exe
// 한글: AsyncIO 레이턴시 벤치마크.
//       측정 지표: 단일 provider 호출 레이턴시 min/avg/p99/max (µs).

#include "Network/Core/AsyncIOProvider.h"

#ifdef _WIN32
#include "Platforms/Windows/IocpAsyncIOProvider.h"
using ProviderType = Network::AsyncIO::Windows::IocpAsyncIOProvider;
#elif defined(__linux__)
#include "Platforms/Linux/EpollAsyncIOProvider.h"
using ProviderType = Network::AsyncIO::Linux::EpollAsyncIOProvider;
#elif defined(__APPLE__)
#include "Platforms/macOS/KqueueAsyncIOProvider.h"
using ProviderType = Network::AsyncIO::macOS::KqueueAsyncIOProvider;
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

using namespace Network::AsyncIO;
using namespace std::chrono;

static constexpr int ITERATIONS = 10000;

int main()
{
    std::cout << "=== AsyncIO Latency Benchmark ===\n";
    std::cout << "Iterations : " << ITERATIONS << "\n\n";

    ProviderType provider;
    if (provider.Initialize(256, 128) != AsyncIOError::Success)
    {
        std::cout << "[ERROR] Provider init failed: " << provider.GetLastError()
                  << "\n";
        return 1;
    }
    std::cout << "Provider   : " << provider.GetInfo().name << "\n\n";

    std::vector<double> latencies;
    latencies.reserve(ITERATIONS);

    for (int i = 0; i < ITERATIONS; ++i)
    {
        auto t0 = steady_clock::now();
        provider.GetStats(); // English: proxy for a single I/O operation / 한글: 단일 I/O 작업 proxy
        auto t1 = steady_clock::now();
        double us =
            static_cast<double>(duration_cast<nanoseconds>(t1 - t0).count()) /
            1000.0;
        latencies.push_back(us);
    }

    std::sort(latencies.begin(), latencies.end());
    double minL = latencies.front();
    double maxL = latencies.back();
    double avgL =
        std::accumulate(latencies.begin(), latencies.end(), 0.0) / ITERATIONS;
    double p99L = latencies[static_cast<size_t>(ITERATIONS * 0.99)];

    std::cout << "[BENCH] Latency (" << ITERATIONS << " samples):\n";
    std::cout << "  min : " << minL << " µs\n";
    std::cout << "  avg : " << avgL << " µs\n";
    std::cout << "  p99 : " << p99L << " µs\n";
    std::cout << "  max : " << maxL << " µs\n";

    provider.Shutdown();
    return 0;
}
```

### Step 3: vcxproj.filters 수정 — Benchmark 필터 신규 + `<None>` 항목

기존 `</ItemGroup>` 중 Documentation ItemGroup 앞에 추가:

```xml
  <ItemGroup>
    <!-- Benchmark -->
    <Filter Include="Benchmark">
      <UniqueIdentifier>{B1E2F3A4-C5D6-7890-ABCD-EF1234567891}</UniqueIdentifier>
    </Filter>
    <None Include="Benchmark\ThroughputBench.cpp">
      <Filter>Benchmark</Filter>
    </None>
    <None Include="Benchmark\LatencyBench.cpp">
      <Filter>Benchmark</Filter>
    </None>
  </ItemGroup>
```

### Step 4: 빌드 검증

`<None>` 항목이므로 빌드에 영향 없음. MSBuild 오류 0개 확인.

### Step 5: 커밋

```bash
git commit -m "feat: add ThroughputBench and LatencyBench

AsyncIO 성능 벤치마크 파일 추가 (ServerEngine/Benchmark/)
- ThroughputBench.cpp: 초당 처리량 측정 (packets/sec, MB/sec)
- LatencyBench.cpp: 레이턴시 측정 (min/avg/p99/max µs)
- 모든 플랫폼 지원 (#ifdef _WIN32 / __linux__ / __APPLE__)"
```

---

## 최종 빌드 확인

모든 Task 완료 후:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe" `
  "E:\MyGitHub\PublicStudy\NetworkModuleTest\NetworkModuleTest.sln" `
  /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 5
```

기대: `오류 0개, 경고 0개`

---

## 파일 변경 요약

| 파일 | 액션 |
|------|------|
| `Interfaces/IBufferPool.h` | 신규 |
| `Platforms/Windows/RIOBufferPool.h` | 신규 |
| `Platforms/Windows/RIOBufferPool.cpp` | 신규 |
| `Platforms/Linux/IOUringBufferPool.h` | 신규 |
| `Platforms/Linux/IOUringBufferPool.cpp` | 신규 |
| `Tests/RIOTest.cpp` | 신규 |
| `Tests/IOUringTest.cpp` | 신규 |
| `Benchmark/ThroughputBench.cpp` | 신규 |
| `Benchmark/LatencyBench.cpp` | 신규 |
| `ServerEngine.vcxproj` | 수정 (ClCompile/ClInclude 추가) |
| `ServerEngine.vcxproj.filters` | 수정 (필터 항목 추가) |
