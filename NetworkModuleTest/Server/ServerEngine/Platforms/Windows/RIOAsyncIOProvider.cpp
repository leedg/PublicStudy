#ifdef _WIN32

#include "RIOAsyncIOProvider.h"
#include "../../Network/Core/PlatformDetect.h"

namespace Network {
namespace AsyncIO {
namespace Windows {

RIOAsyncIOProvider::RIOAsyncIOProvider() : mCompletionQueue(RIO_INVALID_CQ), mInitialized(false) {}
RIOAsyncIOProvider::~RIOAsyncIOProvider() { Shutdown(); }
AsyncIOError RIOAsyncIOProvider::Initialize(size_t queueDepth, size_t maxConcurrent) { return AsyncIOError::Success; }
void RIOAsyncIOProvider::Shutdown() {}
bool RIOAsyncIOProvider::IsInitialized() const { return mInitialized; }
int64_t RIOAsyncIOProvider::RegisterBuffer(const void* ptr, size_t size) { return -1; }
AsyncIOError RIOAsyncIOProvider::UnregisterBuffer(int64_t bufferId) { return AsyncIOError::PlatformNotSupported; }
AsyncIOError RIOAsyncIOProvider::SendAsync(SocketHandle socket, const void* buffer, size_t size, RequestContext context, uint32_t flags) { return AsyncIOError::Success; }
AsyncIOError RIOAsyncIOProvider::RecvAsync(SocketHandle socket, void* buffer, size_t size, RequestContext context, uint32_t flags) { return AsyncIOError::Success; }
AsyncIOError RIOAsyncIOProvider::FlushRequests() { return AsyncIOError::Success; }
int RIOAsyncIOProvider::ProcessCompletions(CompletionEntry* entries, size_t maxEntries, int timeoutMs) { return 0; }
const ProviderInfo& RIOAsyncIOProvider::GetInfo() const { return mInfo; }
ProviderStats RIOAsyncIOProvider::GetStats() const { return mStats; }
const char* RIOAsyncIOProvider::GetLastError() const { return mLastError.c_str(); }
bool RIOAsyncIOProvider::LoadRIOFunctions() { return true; }

std::unique_ptr<AsyncIOProvider> CreateRIOProvider() {
    return std::unique_ptr<AsyncIOProvider>(new RIOAsyncIOProvider());
}

}  // namespace Windows
}  // namespace AsyncIO
}  // namespace Network

#endif

