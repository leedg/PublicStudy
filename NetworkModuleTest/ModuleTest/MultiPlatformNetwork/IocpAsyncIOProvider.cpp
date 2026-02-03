#ifdef _WIN32
// encoding: UTF-8

#include "IocpAsyncIOProvider.h"
#include "PlatformDetect.h"

namespace Network
{
namespace AsyncIO
{
namespace Windows
{

// Light-weight IOCP provider stub to satisfy builds in test modules.
// Full implementations live in ServerEngine platform folder.
std::unique_ptr<AsyncIOProvider> CreateIocpProvider()
{
	return nullptr; // fallback to server-side provider
}

} // namespace Windows
} // namespace AsyncIO
} // namespace Network

#endif
