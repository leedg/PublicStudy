# =============================================================================
# NetworkModule AsyncIO Library - CMake Build Configuration
# =============================================================================
# This CMakeLists.txt builds the AsyncIO provider library and tests
# Supports Windows (IOCP/RIO), Linux (epoll/io_uring), and macOS (kqueue)

cmake_minimum_required(VERSION 3.15)

project(
    NetworkModuleAsyncIO
    VERSION 1.0.0
    LANGUAGES CXX
)

# =============================================================================
# Project Configuration
# =============================================================================

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build Configuration
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)

# =============================================================================
# Platform Detection
# =============================================================================

if(WIN32)
    message(STATUS "Detected Windows platform")
    set(PLATFORM_WINDOWS ON)
elseif(APPLE)
    message(STATUS "Detected macOS platform")
    set(PLATFORM_MACOS ON)
elseif(UNIX AND NOT APPLE)
    message(STATUS "Detected Linux platform")
    set(PLATFORM_LINUX ON)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# =============================================================================
# Source Files
# =============================================================================

# Core source files
set(ASYNCIO_SOURCES
    AsyncIOProvider.h
    AsyncIOProvider.cpp
    PlatformDetect.h
    PlatformDetect.cpp
)

# Platform-specific implementations
if(PLATFORM_WINDOWS)
    list(APPEND ASYNCIO_SOURCES
        IocpAsyncIOProvider.h
        IocpAsyncIOProvider.cpp
        RIOAsyncIOProvider.h
        RIOAsyncIOProvider.cpp
    )
elseif(PLATFORM_LINUX)
    list(APPEND ASYNCIO_SOURCES
        EpollAsyncIOProvider.h
        EpollAsyncIOProvider.cpp
        IOUringAsyncIOProvider.h
        IOUringAsyncIOProvider.cpp
    )
elseif(PLATFORM_MACOS)
    list(APPEND ASYNCIO_SOURCES
        KqueueAsyncIOProvider.h
        KqueueAsyncIOProvider.cpp
    )
endif()

# =============================================================================
# Main Library Target
# =============================================================================

add_library(AsyncIOLib STATIC ${ASYNCIO_SOURCES})

# Include directories
target_include_directories(AsyncIOLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Compiler-specific settings
if(MSVC)
    target_compile_options(AsyncIOLib PRIVATE
        /W4          # Warning level 4
        /WX          # Treat warnings as errors
        /permissive- # Stricter conformance
    )
else()
    target_compile_options(AsyncIOLib PRIVATE
        -Wall        # All warnings
        -Wextra      # Extra warnings
        -Werror      # Treat warnings as errors
        -Wconversion # Conversion warnings
        -Wshadow     # Shadow warnings
    )
endif()

# Platform-specific compiler options
if(PLATFORM_WINDOWS)
    target_compile_definitions(AsyncIOLib PRIVATE
        WIN32_LEAN_AND_MEAN
        _WINDOWS
    )
    target_link_libraries(AsyncIOLib PUBLIC
        ws2_32      # Windows Sockets 2
        mswsock     # Winsock Extensions for Windows
    )
elseif(PLATFORM_LINUX)
    target_compile_options(AsyncIOLib PRIVATE
        -fPIC
    )
    target_link_libraries(AsyncIOLib PUBLIC
        uring       # io_uring library
        pthread     # POSIX threads
    )
elseif(PLATFORM_MACOS)
    target_compile_options(AsyncIOLib PRIVATE
        -fPIC
    )
    target_link_libraries(AsyncIOLib PUBLIC
        pthread     # POSIX threads
    )
endif()

# =============================================================================
# Test Executable (Optional)
# =============================================================================

# Check if GTest is available
find_package(GTest CONFIG)

if(GTest_FOUND)
    message(STATUS "GTest found, building tests")
    
    enable_testing()
    
    add_executable(AsyncIOTests
        AsyncIOTest.cpp
    )
    
    target_link_libraries(AsyncIOTests
        AsyncIOLib
        GTest::GTest
        GTest::Main
    )
    
    # Add test
    add_test(
        NAME AsyncIOTests
        COMMAND AsyncIOTests
    )
else()
    message(WARNING "GTest not found, skipping tests")
    message(STATUS "To enable tests, install GTest:")
    message(STATUS "  Linux: sudo apt-get install libgtest-dev")
    message(STATUS "  macOS: brew install googletest")
    message(STATUS "  Windows: vcpkg install gtest:x64-windows")
endif()

# =============================================================================
# Installation
# =============================================================================

install(TARGETS AsyncIOLib
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    AsyncIOProvider.h
    PlatformDetect.h
    DESTINATION include/RAON/Network/AsyncIO
)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "=== AsyncIO Library Configuration ===")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Source Files: ${ASYNCIO_SOURCES}")
message(STATUS "=====================================")
