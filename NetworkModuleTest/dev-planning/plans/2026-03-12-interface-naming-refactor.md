# Interface Naming Refactor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** (1) `AsyncIOProvider` → `IAsyncIOProvider` (add `I` prefix; it's a pure abstract interface). (2) Merge `IMessageHandler` (pure interface) + `BaseMessageHandler` (concrete base) into a single `IMessageHandler` abstract base class, eliminating the redundant two-layer design.

**Architecture:**
- Change 1 is a pure rename across ~30 files — class name + file name + #include paths + .vcxproj entries.
- Change 2 collapses the 3-layer message hierarchy (`IMessageHandler → BaseMessageHandler → TestServerMessageHandler`) into 2 layers (`IMessageHandler → TestServerMessageHandler`). `IMessageHandler` absorbs `BaseMessageHandler`'s concrete implementation, becoming an abstract base class (no pure virtuals; all methods have defaults, subclasses override as needed).

**Tech Stack:** C++17, MSBuild/VS2022, Windows/Linux/macOS cross-platform

---

## Change 1 — `AsyncIOProvider` → `IAsyncIOProvider`

### Task 1: Create feature branch

**Step 1: Create and switch to branch**
```bash
cd "E:/MyGitHub/PublicStudy/NetworkModuleTest"
git checkout -b feature/interface-naming-refactor
```

---

### Task 2: Rename the file (git mv) + update class definition

**Files:**
- Rename: `Server/ServerEngine/Network/Core/AsyncIOProvider.h` → `IAsyncIOProvider.h`
- Rename: `Server/ServerEngine/Network/Core/AsyncIOProvider.cpp` → `IAsyncIOProvider.cpp`

**Step 1: git mv the files**
```bash
cd "E:/MyGitHub/PublicStudy/NetworkModuleTest"
git mv "Server/ServerEngine/Network/Core/AsyncIOProvider.h" "Server/ServerEngine/Network/Core/IAsyncIOProvider.h"
git mv "Server/ServerEngine/Network/Core/AsyncIOProvider.cpp" "Server/ServerEngine/Network/Core/IAsyncIOProvider.cpp"
```

**Step 2: In `IAsyncIOProvider.h`, rename class and factory signatures**

Find and replace ALL occurrences:
- `class AsyncIOProvider` → `class IAsyncIOProvider`
- `std::unique_ptr<AsyncIOProvider>` → `std::unique_ptr<IAsyncIOProvider>`
- Also update comments: `// 추상 인터페이스: AsyncIOProvider` → `// 추상 인터페이스: IAsyncIOProvider`

**Step 3: In `IAsyncIOProvider.cpp`, update class references**

In the `.cpp`, all forward declarations and function signatures that mention `AsyncIOProvider` as a type:
- `std::unique_ptr<AsyncIOProvider> CreateAsyncIOProvider(...)` → `std::unique_ptr<IAsyncIOProvider> CreateAsyncIOProvider(...)`

---

### Task 3: Update `Network/Core` headers that include `AsyncIOProvider.h`

**Files:**
- `Server/ServerEngine/Network/Core/BaseNetworkEngine.h`
- `Server/ServerEngine/Network/Core/NetworkEngine.h`
- `Server/ServerEngine/Network/Core/Session.h`
- `Server/ServerEngine/Network/Core/PlatformDetect.h`

**Step 1: Replace in each file**
- `#include "AsyncIOProvider.h"` → `#include "IAsyncIOProvider.h"`
- `AsyncIO::AsyncIOProvider` → `AsyncIO::IAsyncIOProvider`
- `shared_ptr<AsyncIO::AsyncIOProvider>` → `shared_ptr<AsyncIO::IAsyncIOProvider>`

**Session.h has two occurrences:**
- Line 146: `void SetAsyncProvider(std::shared_ptr<AsyncIO::AsyncIOProvider> provider)` → `IAsyncIOProvider`
- Line 258: `std::shared_ptr<AsyncIO::AsyncIOProvider> mAsyncProvider;` → `IAsyncIOProvider`

---

### Task 4: Update `Network/Core/Session.cpp`

**File:** `Server/ServerEngine/Network/Core/Session.cpp`

**Step 1:** Find all `AsyncIO::AsyncIOProvider` or bare `AsyncIOProvider` usages → `AsyncIO::IAsyncIOProvider`

---

### Task 5: Update Windows platform providers

**Files:**
- `Server/ServerEngine/Platforms/Windows/IocpAsyncIOProvider.h`
- `Server/ServerEngine/Platforms/Windows/IocpAsyncIOProvider.cpp`
- `Server/ServerEngine/Platforms/Windows/RIOAsyncIOProvider.h`
- `Server/ServerEngine/Platforms/Windows/RIOAsyncIOProvider.cpp`

**Step 1: Each header**
- `#include "Network/Core/AsyncIOProvider.h"` → `#include "Network/Core/IAsyncIOProvider.h"`
- `class IocpAsyncIOProvider : public AsyncIOProvider` → `public IAsyncIOProvider`
- `class RIOAsyncIOProvider : public AsyncIOProvider` → `public IAsyncIOProvider`

**Step 2: Each .cpp**
- Any `AsyncIOProvider` type reference → `IAsyncIOProvider`

---

### Task 6: Update Linux platform providers

**Files:**
- `Server/ServerEngine/Platforms/Linux/EpollAsyncIOProvider.h`
- `Server/ServerEngine/Platforms/Linux/EpollAsyncIOProvider.cpp`
- `Server/ServerEngine/Platforms/Linux/IOUringAsyncIOProvider.h`
- `Server/ServerEngine/Platforms/Linux/IOUringAsyncIOProvider.cpp`

**Step 1: Each header**
- `#include "AsyncIOProvider.h"` → `#include "IAsyncIOProvider.h"` *(or `"Network/Core/IAsyncIOProvider.h"` depending on relative path)*
- `class EpollAsyncIOProvider : public AsyncIOProvider` → `public IAsyncIOProvider`
- `class IOUringAsyncIOProvider : public AsyncIOProvider` → `public IAsyncIOProvider`

**Step 2: Each .cpp** — same type reference replacements

---

### Task 7: Update macOS platform provider

**Files:**
- `Server/ServerEngine/Platforms/macOS/KqueueAsyncIOProvider.h`
- `Server/ServerEngine/Platforms/macOS/KqueueAsyncIOProvider.cpp`

**Step 1:**
- `#include "AsyncIOProvider.h"` → `#include "IAsyncIOProvider.h"`
- `class KqueueAsyncIOProvider : public AsyncIOProvider` → `public IAsyncIOProvider`

---

### Task 8: Update Network Engine platform implementations

**Files:**
- `Server/ServerEngine/Network/Platforms/WindowsNetworkEngine.cpp`
- `Server/ServerEngine/Network/Platforms/LinuxNetworkEngine.cpp`
- `Server/ServerEngine/Network/Platforms/macOSNetworkEngine.cpp`

**Step 1:** In each file, replace `AsyncIOProvider` type references → `IAsyncIOProvider`

---

### Task 9: Update remaining ServerEngine consumers

**Files:**
- `Server/ServerEngine/Core/Memory/RIOBufferPool.h` (if it includes AsyncIOProvider.h)
- `Server/ServerEngine/Tests/AsyncIOTest.cpp`
- ~~`Server/DBServer/include/DBServer.h`~~ (삭제됨 — 2026-03-26)
- ~~`Server/DBServer/src/DBServer.cpp`~~ (삭제됨 — 2026-03-26)
- `Server/ServerEngine/main.cpp`
- Any test files: `Server/Tests/EpollTest/EpollTest.cpp`, `IOCPTest/IOCPTest.cpp`, `IOUringTest/IOUringTest.cpp`, `RIOTest/RIOTest.cpp`

**Step 1:** Replace `AsyncIOProvider.h` includes → `IAsyncIOProvider.h`, type refs → `IAsyncIOProvider`

---

### Task 10: Update ModuleTest copies

**Files:**
- `ModuleTest/MultiPlatformNetwork/AsyncIOProvider.h` → `IAsyncIOProvider.h` (git mv)
- `ModuleTest/MultiPlatformNetwork/AsyncIOProvider.cpp` → `IAsyncIOProvider.cpp` (git mv)
- `ModuleTest/MultiPlatformNetwork/IocpAsyncIOProvider.h` (include + inheritance)
- `ModuleTest/MultiPlatformNetwork/RIOAsyncIOProvider.h` (include + inheritance)
- `ModuleTest/MultiPlatformNetwork/EpollAsyncIOProvider.h` (include + inheritance)
- `ModuleTest/MultiPlatformNetwork/IOUringAsyncIOProvider.h` (include + inheritance)
- `ModuleTest/MultiPlatformNetwork/KqueueAsyncIOProvider.h` (include + inheritance)
- `ModuleTest/MultiPlatformNetwork/AsyncIOTest.cpp` (type refs)
- `ModuleTest/MultiPlatformNetwork/PlatformDetect.h` (include)
- All `*AsyncIOProvider.cpp` files in ModuleTest (type refs)

**Step 1:** git mv the two base files
**Step 2:** In each platform header, same include + inheritance rename as Tasks 5-7

---

### Task 11: Update .vcxproj files

**Files:**
- `Server/ServerEngine/ServerEngine.vcxproj`
- `Server/ServerEngine/ServerEngine.vcxproj.filters`
- `ModuleTest/MultiPlatformNetwork/MultiPlatformNetwork.vcxproj`
- `ModuleTest/MultiPlatformNetwork/MultiPlatformNetwork.vcxproj.filters`

**Step 1:** In `.vcxproj`, find `<ClInclude Include="...\AsyncIOProvider.h" />` → `IAsyncIOProvider.h`
Find `<ClCompile Include="...\AsyncIOProvider.cpp" />` → `IAsyncIOProvider.cpp`

**Step 2:** In `.vcxproj.filters`, same replacements (these files have filter assignments per file path)

---

### Task 12: Commit Change 1

```bash
cd "E:/MyGitHub/PublicStudy/NetworkModuleTest"
git add -A
git commit -m "refactor: AsyncIOProvider → IAsyncIOProvider (I 접두어 추가)"
```

---

## Change 2 — Merge `BaseMessageHandler` + `IMessageHandler`

**Design:** Absorb `BaseMessageHandler` into `IMessageHandler`. The merged class:
- Stays in `Server/ServerEngine/Interfaces/IMessageHandler.h`
- Gets a new `Interfaces/IMessageHandler.cpp` (absorbs `BaseMessageHandler.cpp` content)
- `Network::Implementations::BaseMessageHandler` and its files are deleted
- `TestServerMessageHandler` inherits from `Network::Interfaces::IMessageHandler` directly
- `MessageHeader` struct (currently anonymous in `BaseMessageHandler.cpp`) stays in the new `.cpp`
- `MessageCallback` using-alias moves into `IMessageHandler`

---

### Task 13: Rewrite `IMessageHandler.h`

**File:** `Server/ServerEngine/Interfaces/IMessageHandler.h`

**New content** (replace the class definition entirely):

```cpp
#pragma once

#include "Message.h"
#include "MessageType_enum.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Network::Interfaces
{

/**
 * Abstract base class for message handling.
 * Provides default implementations for all message operations.
 * Derive from this class and call RegisterHandler() to handle specific message types.
 */
class IMessageHandler
{
  public:
    using MessageCallback = std::function<void(const Message &)>;

    IMessageHandler();
    virtual ~IMessageHandler() = default;

    // ─── Core message operations (overridable) ────────────────────────────

    virtual bool ProcessMessage(ConnectionId connectionId, const uint8_t *data,
                                size_t size);

    virtual std::vector<uint8_t> CreateMessage(MessageType type,
                                               ConnectionId connectionId,
                                               const void *data, size_t size);

    virtual uint64_t GetCurrentTimestamp() const;

    virtual bool ValidateMessage(const uint8_t *data, size_t size) const;

    // ─── Handler registration ─────────────────────────────────────────────

    bool RegisterHandler(MessageType type, MessageCallback callback);
    void UnregisterHandler(MessageType type);

  protected:
    virtual bool ParseMessage(ConnectionId connectionId, const uint8_t *data,
                              size_t size, Message &outMessage);

    virtual std::vector<uint8_t> SerializeMessage(const Message &message);

    static MessageType GetMessageType(const uint8_t *data, size_t size);

  private:
    std::unordered_map<MessageType, MessageCallback> mHandlers;
    mutable std::mutex mMutex;
    uint32_t mNextMessageId;
};

} // namespace Network::Interfaces
```

---

### Task 14: Create `IMessageHandler.cpp` in `Interfaces/`

**File to create:** `Server/ServerEngine/Interfaces/IMessageHandler.cpp`

**Content:** Move everything from `BaseMessageHandler.cpp`, updating namespace and class name:
- `namespace Network::Implementations` → `namespace Network::Interfaces`
- All `BaseMessageHandler::` method prefixes → `IMessageHandler::`
- `#include "BaseMessageHandler.h"` → `#include "IMessageHandler.h"`
- Keep `MessageHeader` struct with `#pragma pack(push,1)` as-is

---

### Task 15: Delete `BaseMessageHandler` files

```bash
cd "E:/MyGitHub/PublicStudy/NetworkModuleTest"
git rm "Server/ServerEngine/Implementations/Protocols/BaseMessageHandler.h"
git rm "Server/ServerEngine/Implementations/Protocols/BaseMessageHandler.cpp"
```

---

### Task 16: Update `TestServerMessageHandler.h`

**File:** `Server/TestServer/include/TestMessageHandler.h`

**Step 1:** Change include and base class:
```cpp
// Before:
#include "../../ServerEngine/Implementations/Protocols/BaseMessageHandler.h"
// ...
class TestServerMessageHandler
    : public Network::Implementations::BaseMessageHandler

// After:
#include "../../ServerEngine/Interfaces/IMessageHandler.h"
// ...
class TestServerMessageHandler
    : public Network::Interfaces::IMessageHandler
```

---

### Task 17: Update `.vcxproj` for the message handler change

**File:** `Server/ServerEngine/ServerEngine.vcxproj`

**Step 1:** Remove `BaseMessageHandler.h` and `BaseMessageHandler.cpp` entries
**Step 2:** Add `IMessageHandler.cpp` entry (under `<ClCompile>`)
**Step 3:** `IMessageHandler.h` should already be present; confirm it's there

**File:** `Server/ServerEngine/ServerEngine.vcxproj.filters`

**Step 1:** Remove `BaseMessageHandler.h` and `BaseMessageHandler.cpp` filter entries
**Step 2:** Add `IMessageHandler.cpp` to the same filter group as `IMessageHandler.h`

---

### Task 18: Commit Change 2

```bash
cd "E:/MyGitHub/PublicStudy/NetworkModuleTest"
git add -A
git commit -m "refactor: BaseMessageHandler + IMessageHandler → IMessageHandler 단일 추상 베이스로 통합"
```

---

### Task 19: Build verification

```bash
powershell.exe -Command "& 'C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe' 'E:\\MyGitHub\\PublicStudy\\NetworkModuleTest\\NetworkModuleTest.sln' /p:Configuration=Debug /p:Platform=x64 /m /nologo 2>&1 | Select-Object -Last 20"
```

Expected: `0 Error(s)`, `0 Warning(s)`

If there are errors, fix them and amend or add a fixup commit before proceeding.

---

### Task 20: Push branch

```bash
cd "E:/MyGitHub/PublicStudy/NetworkModuleTest"
git push -u origin feature/interface-naming-refactor
```
