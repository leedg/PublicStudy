#ifdef _WIN32

#include "IocpAsyncIOProvider.h"
#include "../../Network/Core/PlatformDetect.h"

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

IocpAsyncIOProvider::IocpAsyncIOProvider()
	: mCompletionPort(INVALID_HANDLE_VALUE), mInitialized(false)
{
}
IocpAsyncIOProvider::~IocpAsyncIOProvider() { Shutdown(); }
AsyncIOError IocpAsyncIOProvider::Initialize(size_t queueDepth,
											 size_t maxConcurrent)
{
	return AsyncIOError::Success;
}
void IocpAsyncIOProvider::Shutdown() {}
bool IocpAsyncIOProvider::IsInitialized() const { return mInitialized; }
int64_t IocpAsyncIOProvider::RegisterBuffer(const void *ptr, size_t size)
{
	return -1;
}
AsyncIOError IocpAsyncIOProvider::UnregisterBuffer(int64_t bufferId)
{
	return AsyncIOError::PlatformNotSupported;
}
AsyncIOError IocpAsyncIOProvider::SendAsync(SocketHandle socket,
											const void *buffer, size_t size,
											RequestContext context,
											uint32_t flags)
{
	return AsyncIOError::Success;
}
AsyncIOError IocpAsyncIOProvider::RecvAsync(SocketHandle socket, void *buffer,
											size_t size, RequestContext context,
											uint32_t flags)
{
	return AsyncIOError::Success;
}
AsyncIOError IocpAsyncIOProvider::FlushRequests()
{
	return AsyncIOError::Success;
}
int IocpAsyncIOProvider::ProcessCompletions(CompletionEntry *entries,
											size_t maxEntries, int timeoutMs)
{
	return 0;
}
const ProviderInfo &IocpAsyncIOProvider::GetInfo() const { return mInfo; }
ProviderStats IocpAsyncIOProvider::GetStats() const { return mStats; }
const char *IocpAsyncIOProvider::GetLastError() const
{
	return mLastError.c_str();
}

std::unique_ptr<AsyncIOProvider> CreateIocpProvider()
{
	return std::unique_ptr<AsyncIOProvider>(new IocpAsyncIOProvider());
}

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif
