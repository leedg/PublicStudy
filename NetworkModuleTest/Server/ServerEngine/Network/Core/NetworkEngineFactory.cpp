// English: NetworkEngine factory implementation
// 한글: NetworkEngine 팩토리 구현

#include "NetworkEngine.h"
#include "../Platforms/WindowsNetworkEngine.h"
#include "../Platforms/LinuxNetworkEngine.h"
#include "../Platforms/macOSNetworkEngine.h"
#include "../../Utils/Logger.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __linux__
#include <sys/utsname.h>
#endif

namespace Network::Core
{

// =============================================================================
// English: Factory function
// 한글: 팩토리 함수
// =============================================================================

std::unique_ptr<INetworkEngine>
CreateNetworkEngine(const std::string &engineType)
{
#ifdef _WIN32
	// English: Windows platform
	// 한글: Windows 플랫폼

	if (engineType == "auto" || engineType == "default" || engineType.empty())
	{
		// English: Auto-detect best backend for Windows
		// 한글: Windows 최적 백엔드 자동 감지

		// English: Try RIO first (Windows 8+)
		// 한글: RIO 먼저 시도 (Windows 8+)
		OSVERSIONINFOEX osvi;
		ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
		osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

#pragma warning(push)
#pragma warning(disable : 4996) // GetVersionEx is deprecated
		if (GetVersionEx((OSVERSIONINFO *)&osvi))
		{
			// Windows 8+ = Major version >= 6 and Minor >= 2
			if (osvi.dwMajorVersion > 6 ||
				(osvi.dwMajorVersion == 6 && osvi.dwMinorVersion >= 2))
			{
				Utils::Logger::Info(
					"Windows 8+ detected, using RIO backend (auto)");
				return std::make_unique<Platforms::WindowsNetworkEngine>(
					Platforms::WindowsNetworkEngine::Mode::RIO);
			}
		}
#pragma warning(pop)

		// English: Fallback to IOCP
		// 한글: IOCP로 폴백
		Utils::Logger::Info("Using IOCP backend (auto)");
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
	// 한글: Linux 플랫폼

	if (engineType == "auto" || engineType == "default" || engineType.empty())
	{
		// English: Auto-detect best backend for Linux
		// 한글: Linux 최적 백엔드 자동 감지

		// English: Try io_uring first (Linux 5.1+)
		// 한글: io_uring 먼저 시도 (Linux 5.1+)
		struct utsname unameData;
		if (uname(&unameData) == 0)
		{
			int major = 0, minor = 0;
			sscanf(unameData.release, "%d.%d", &major, &minor);

			// Linux 5.1+ supports io_uring
			if (major > 5 || (major == 5 && minor >= 1))
			{
				Utils::Logger::Info(
					"Linux 5.1+ detected, using io_uring backend (auto)");
				return std::make_unique<Platforms::LinuxNetworkEngine>(
					Platforms::LinuxNetworkEngine::Mode::IOUring);
			}
		}

		// English: Fallback to epoll
		// 한글: epoll로 폴백
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
	// 한글: macOS 플랫폼

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
// 한글: 사용 가능한 엔진 타입 조회
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
