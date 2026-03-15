// Main entry point for ServerEngine

#include "Network/Core/AsyncIOProvider.h"
#include "Network/Core/PlatformDetect.h"
#include <iostream>

int main()
{
	std::cout << "ServerEngine - Network Module Test" << std::endl;

	// Simple test
	auto platform = Network::AsyncIO::Platform::DetectPlatform();
	std::cout << "Current platform detected successfully." << std::endl;

	return 0;
}
