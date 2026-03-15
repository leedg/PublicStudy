#pragma once

// Linux-specific NetworkEngine implementation
//
// Supports two modes:
// - epoll: Standard event notification (all Linux versions)
// - io_uring: Modern async I/O (Linux 5.1+, high performance)
//

#ifdef __linux__

#include "../Core/BaseNetworkEngine.h"
#include <thread>
#include <vector>

namespace Network::Platforms
{

// =============================================================================
// Linux NetworkEngine
// =============================================================================

class LinuxNetworkEngine : public Core::BaseNetworkEngine
{
  public:
	// I/O backend mode
	enum class Mode
	{
		Epoll,    // Standard epoll
		IOUring   // io_uring
	};

	/**
	 * Constructor
	 * @param mode I/O backend mode (Epoll or IOUring)
	 */
	explicit LinuxNetworkEngine(Mode mode = Mode::Epoll);
	virtual ~LinuxNetworkEngine();

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
	// Create listen socket
	bool CreateListenSocket();

	// Queue recv for a session
	bool QueueRecv(const Core::SessionRef &session);

	// Worker thread function
	void WorkerThread();

  private:
	// I/O mode
	Mode mMode;

	// Listen socket
	int mListenSocket;

	// Accept loop backoff (ms) - member to avoid static variable bug
	int mAcceptBackoffMs;

	// Accept thread
	std::thread mAcceptThread;

	// Worker threads (for completion processing)
	std::vector<std::thread> mWorkerThreads;
};

} // namespace Network::Platforms

#endif // __linux__
