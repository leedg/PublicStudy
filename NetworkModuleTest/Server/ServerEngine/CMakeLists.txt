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
# Find Dependencies
# =============================================================================

# Find Protobuf
find_package(Protobuf CONFIG REQUIRED)
if(Protobuf_FOUND)
    message(STATUS "Protobuf found: ${Protobuf_VERSION}")
else()
    message(FATAL_ERROR "Protobuf not found. Please install libprotobuf-dev and protobuf-compiler")
endif()

# =============================================================================
# Source Files
# =============================================================================

# Core source files
set(ASYNCIO_SOURCES
    Core/AsyncIOProvider.h
    Core/AsyncIOProvider.cpp
    Core/PlatformDetect.h
    Core/PlatformDetect.cpp
    Core/Types.h
)

# Protocol source files
set(PROTOCOL_SOURCES
    Protocols/ping.proto
    Protocols/PingPong.h
    Protocols/MessageHandler.h
)

# Platform-specific implementations
if(PLATFORM_WINDOWS)
    list(APPEND ASYNCIO_SOURCES
        Platforms/Windows/IocpAsyncIOProvider.h
        Platforms/Windows/IocpAsyncIOProvider.cpp
        Platforms/Windows/RIOAsyncIOProvider.h
        Platforms/Windows/RIOAsyncIOProvider.cpp
    )
elseif(PLATFORM_LINUX)
    list(APPEND ASYNCIO_SOURCES
        Platforms/Linux/EpollAsyncIOProvider.h
        Platforms/Linux/EpollAsyncIOProvider.cpp
        Platforms/Linux/IOUringAsyncIOProvider.h
        Platforms/Linux/IOUringAsyncIOProvider.cpp
    )
    # English: io_uring option — enabled by default on Linux; requires liburing-dev.
    #          Without liburing the factory silently falls back to epoll.
    # 한글: io_uring 옵션 — Linux에서 기본 활성화; liburing-dev 필요.
    #       없으면 팩토리가 자동으로 epoll로 폴백.
    option(ENABLE_IO_URING "Enable io_uring backend (Linux)" ON)

    if(ENABLE_IO_URING)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBURING liburing)
        if(LIBURING_FOUND)
            message(STATUS "liburing found (${LIBURING_VERSION}) — io_uring backend enabled")
            set(HAVE_IO_URING ON)
        else()
            # Fallback: manual find
            find_library(LIBURING_LIB NAMES uring liburing)
            find_path(LIBURING_INCLUDE NAMES liburing.h)
            if(LIBURING_LIB AND LIBURING_INCLUDE)
                message(STATUS "liburing found: ${LIBURING_LIB} — io_uring backend enabled")
                set(HAVE_IO_URING ON)
                set(LIBURING_LIBRARIES ${LIBURING_LIB})
                set(LIBURING_INCLUDE_DIRS ${LIBURING_INCLUDE})
            else()
                message(WARNING "liburing not found (apt install liburing-dev) — io_uring disabled, using epoll only")
                set(HAVE_IO_URING OFF)
                set(ENABLE_IO_URING OFF)
            endif()
        endif()
    else()
        message(STATUS "io_uring backend disabled (ENABLE_IO_URING=OFF)")
        set(HAVE_IO_URING OFF)
    endif()
elseif(PLATFORM_MACOS)
    list(APPEND ASYNCIO_SOURCES
        Platforms/macOS/KqueueAsyncIOProvider.h
        Platforms/macOS/KqueueAsyncIOProvider.cpp
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
        ${CMAKE_CURRENT_BINARY_DIR}
        ${Protobuf_INCLUDE_DIRS}
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
        protobuf::libprotobuf
    )
elseif(PLATFORM_LINUX)
    target_compile_options(AsyncIOLib PRIVATE
        -fPIC
    )
    if(HAVE_IO_URING)
        target_compile_definitions(AsyncIOLib PUBLIC HAVE_IO_URING HAVE_LIBURING)
        target_include_directories(AsyncIOLib PUBLIC ${LIBURING_INCLUDE_DIRS})
        target_link_libraries(AsyncIOLib PUBLIC
            ${LIBURING_LIBRARIES}  # io_uring library (liburing-dev)
            pthread                # POSIX threads
            protobuf::libprotobuf
        )
    else()
        target_link_libraries(AsyncIOLib PUBLIC
            pthread           # POSIX threads (epoll-only mode)
            protobuf::libprotobuf
        )
    endif()
elseif(PLATFORM_MACOS)
    target_compile_options(AsyncIOLib PRIVATE
        -fPIC
    )
    target_link_libraries(AsyncIOLib PUBLIC
        pthread     # POSIX threads
        protobuf::libprotobuf
    )
endif()

# =============================================================================
# Protobuf Generation
# =============================================================================

# Generate protobuf code
set(PROTO_FILES
    Protocols/ping.proto
)

foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    
    protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})
    
    list(APPEND ASYNCIO_SOURCES ${PROTO_SRCS})
    list(APPEND PROTO_HEADERS ${PROTO_HDRS})
endforeach()

message(STATUS "Generated Protobuf sources: ${PROTO_SRCS}")
message(STATUS "Generated Protobuf headers: ${PROTO_HDRS}")

# =============================================================================
# Test Executable (Optional)
# =============================================================================

# Check if GTest is available
find_package(GTest CONFIG)

if(GTest_FOUND)
    message(STATUS "GTest found, building tests")
    
    enable_testing()
    
    add_executable(AsyncIOTests
        Tests/AsyncIOTest.cpp
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
    Core/AsyncIOProvider.h
    Core/PlatformDetect.h
    Core/Types.h
    ${PROTO_HEADERS}
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
