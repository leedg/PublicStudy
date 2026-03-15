#pragma once

// Timer utility for time measurement

#include "NetworkTypes.h"
#include <chrono>

namespace Network::Utils
{
// =============================================================================
// Timer - simple stopwatch-style timer
// =============================================================================

class Timer
{
public:
	// Constructor - starts the timer
	Timer() : mStartTime(GetCurrentTimestamp()) {}

	// Get elapsed time since timer creation/reset
	Timestamp GetElapsedTime() const
	{
		return GetCurrentTimestamp() - mStartTime;
	}

	// Reset the timer to current time
	void Reset() { mStartTime = GetCurrentTimestamp(); }

	// Get current timestamp in milliseconds
	static Timestamp GetCurrentTimestamp()
	{
		// Use steady_clock for elapsed time measurement to prevent issues
		// caused by NTP clock adjustments (system time going backwards).
		auto now = std::chrono::steady_clock::now();
		auto duration = now.time_since_epoch();
		return std::chrono::duration_cast<std::chrono::milliseconds>(duration)
			.count();
	}

private:
	Timestamp mStartTime;
};

} // namespace Network::Utils
