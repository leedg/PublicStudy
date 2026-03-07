# 2026-03-06 macOS Accept Loop EAGAIN Handling

## Scope
- Branch: `fix/macos-accept-eagain-handling`
- Code commit: `338d4c1`
- File: `Server/ServerEngine/Network/Platforms/macOSNetworkEngine.cpp`

## Why this is required
- The macOS accept loop uses a non-blocking listen socket.
- `accept()` can return `EAGAIN` or `EWOULDBLOCK` when there is no pending connection.
- Before this fix, that state was treated as an error and entered exponential backoff.
- Linux already handles these errno values as expected non-error flow.

## Implemented changes
1. Added `<cerrno>` include for errno constants.
2. Captured `errno` immediately into `acceptError`.
3. Kept shutdown handling for `EINTR` and `EBADF`.
4. Added explicit handling for `EAGAIN` and `EWOULDBLOCK`:
   - sleep `1ms`
   - continue accept loop without error logging
5. Changed error logging to use `strerror(acceptError)` for stable errno reporting.

## Platform impact
- Affects only macOS implementation (`#ifdef __APPLE__` source file).
- No code-path change for Windows or Linux.

## Validation status
- Runtime test was not executed in this workspace (Windows environment).
- Recommended macOS verification:
  - Run server with no clients for 1-2 minutes.
  - Confirm no repeated `Accept failed: Resource temporarily unavailable` logs.
  - Connect/disconnect a client and verify normal accept behavior is unchanged.
