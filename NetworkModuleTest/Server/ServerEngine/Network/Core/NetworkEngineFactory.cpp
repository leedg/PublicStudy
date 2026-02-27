// English: NetworkEngine factory implementation

#include "NetworkEngine.h"
#include "PlatformDetect.h"
#include "../Platforms/WindowsNetworkEngine.h"
#include "../Platforms/LinuxNetworkEngine.h"
#include "../Platforms/macOSNetworkEngine.h"
#include "../../Utils/Logger.h"

#ifdef __linux__
#include <sys/utsname.h>
#endif

namespace Network::Core
{

// =============================================================================
// English: Factory function
// =============================================================================

std::unique_ptr<INetworkEngine>
CreateNetworkEngine(const std::string &engineType)
{
#ifdef _WIN32
	// English: Windows platform
	if (engineType == "auto" || engineType == "default" || engineType.empty())
	{
		if (Network::AsyncIO::Platform::IsWindowsRIOSupported())
		{
			Utils::Logger::Info("Windows RIO supported, using RIO backend (auto)");
			return std::make_unique<Platforms::WindowsNetworkEngine>(
				Platforms::WindowsNetworkEngine::Mode::RIO);
		}

		Utils::Logger::Info("Using IOCP backend (auto fallback)");
		return std::make_unique<Platforms::WindowsNetworkEngine>(
			Platforms::WindowsNetworkEngine::Mode::IOCP);
	}
	else if (engineType == "iocp")
	{
		Utils::Logger::Info("Using IOCP backend (explicit)");
		return std::make_unique<Platforms::WindowsNetworkEngine>(
			Platforms::WindowsNetworkEngine::Mode::IOCP);
	}
	else if (engineType == "rio")
	{
		Utils::Logger::Info("Using RIO backend (explicit)");
		return std::make_unique<Platforms::WindowsNetworkEngine>(
			Platforms::WindowsNetworkEngine::Mode::RIO);
	}
	else
	{
		Utils::Logger::Error("Unknown engine type: " + engineType +
							 " (available: iocp, rio, auto)");
		return nullptr;
	}

#elif defined(__linux__)
	// English: Linux platform
	if (engineType == "auto" || engineType == "default" || engineType.empty())
	{
		// English: Auto-detect best backend for Linux

		// English: Try io_uring first (Linux 5.1+)
		struct utsname unameData;
		if (uname(&unameData) == 0)
		{
			int major = 0, minor = 0;
			const int parsed = sscanf(unameData.release, "%d.%d", &major, &minor);

			// Linux 5.1+ supports io_uring
			if (parsed == 2 && (major > 5 || (major == 5 && minor >= 1)))
			{
				Utils::Logger::Info(
					"Linux 5.1+ detected, using io_uring backend (auto)");
				return std::make_unique<Platforms::LinuxNetworkEngine>(
					Platforms::LinuxNetworkEngine::Mode::IOUring);
			}
		}

		// English: Fallback to epoll
		Utils::Logger::Info("Using epoll backend (auto)");
		return std::make_unique<Platforms::LinuxNetworkEngine>(
			Platforms::LinuxNetworkEngine::Mode::Epoll);
	}
	else if (engineType == "epoll")
	{
		Utils::Logger::Info("Using epoll backend (explicit)");
		return std::make_unique<Platforms::LinuxNetworkEngine>(
			Platforms::LinuxNetworkEngine::Mode::Epoll);
	}
	else if (engineType == "io_uring")
	{
		Utils::Logger::Info("Using io_uring backend (explicit)");
		return std::make_unique<Platforms::LinuxNetworkEngine>(
			Platforms::LinuxNetworkEngine::Mode::IOUring);
	}
	else
	{
		Utils::Logger::Error("Unknown engine type: " + engineType +
							 " (available: epoll, io_uring, auto)");
		return nullptr;
	}

#elif defined(__APPLE__)
	// English: macOS platform
	if (engineType == "auto" || engineType == "default" || engineType.empty() ||
		engineType == "kqueue")
	{
		Utils::Logger::Info("Using kqueue backend");
		return std::make_unique<Platforms::macOSNetworkEngine>();
	}
	else
	{
		Utils::Logger::Error("Unknown engine type: " + engineType +
							 " (available: kqueue, auto)");
		return nullptr;
	}

#else
	Utils::Logger::Error("Unsupported platform");
	return nullptr;
#endif
}

// =============================================================================
// English: Get available engine types
// =============================================================================

std::vector<std::string> GetAvailableEngineTypes()
{
	std::vector<std::string> types;

#ifdef _WIN32
	types.push_back("iocp");
	types.push_back("rio");
	types.push_back("auto");

#elif defined(__linux__)
	types.push_back("epoll");
	types.push_back("io_uring");
	types.push_back("auto");

#elif defined(__APPLE__)
	types.push_back("kqueue");
	types.push_back("auto");
#endif

	return types;
}

} // namespace Network::Core
