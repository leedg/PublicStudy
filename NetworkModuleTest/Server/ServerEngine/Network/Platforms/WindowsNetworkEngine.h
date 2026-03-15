#pragma once

// Windows-specific NetworkEngine implementation
//
// Supports two modes:
// - IOCP: Standard I/O Completion Port (all Windows versions)
// - RIO: Registered I/O (Windows 8+, high performance)
//

#ifdef _WIN32

#include "../Core/BaseNetworkEngine.h"
#include <thread>
#include <vector>

namespace Network::Platforms
{

// =============================================================================
// Windows NetworkEngine
// =============================================================================

class WindowsNetworkEngine : public Core::BaseNetworkEngine
{
  public:
	// I/O backend mode
	enum class Mode
	{
		IOCP, // Standard IOCP
		RIO   // Registered I/O
	};

	/**
	 * Constructor
	 * @param mode I/O backend mode (IOCP or RIO)
	 */
	explicit WindowsNetworkEngine(Mode mode = Mode::IOCP);
	virtual ~WindowsNetworkEngine();

  protected:
	// =====================================================================
	// Platform-specific implementation
	// =====================================================================

	bool InitializePlatform() override;
	void ShutdownPlatform() override;
	bool StartPlatformIO() override;
	void StopPlatformIO() override;
	void AcceptLoop() override;
	void ProcessCompletions() override;

  private:
	// Initialize Winsock
	bool InitializeWinsock();

	// Create listen socket
	bool CreateListenSocket();

	// Worker thread function
	void WorkerThread();

  private:
	// I/O mode
	Mode mMode;

	// Listen socket
	SOCKET mListenSocket;

	// Accept loop backoff (ms) - member to avoid static variable bug
	int mAcceptBackoffMs;

	// Accept thread
	std::thread mAcceptThread;

	// Worker threads (for completion processing)
	std::vector<std::thread> mWorkerThreads;
};

} // namespace Network::Platforms

#endif // _WIN32
