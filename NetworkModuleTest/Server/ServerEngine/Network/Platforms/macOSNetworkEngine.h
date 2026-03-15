#pragma once

// macOS-specific NetworkEngine implementation
//
// Uses kqueue for high-performance event notification

#ifdef __APPLE__

#include "../Core/BaseNetworkEngine.h"
#include <thread>
#include <vector>

namespace Network::Platforms
{

// =============================================================================
// macOS NetworkEngine
// =============================================================================

class macOSNetworkEngine : public Core::BaseNetworkEngine
{
  public:
	/**
	 * Constructor
	 */
	explicit macOSNetworkEngine();
	virtual ~macOSNetworkEngine();

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

#endif // __APPLE__
