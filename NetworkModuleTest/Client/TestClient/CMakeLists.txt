# =============================================================================
# TestClient - Cross-Platform TCP Test Client
# =============================================================================
# English: Builds the TestClient executable for Windows, Linux, and macOS
# 한글: Windows, Linux, macOS용 TestClient 실행 파일 빌드

cmake_minimum_required(VERSION 3.15)

project(
    TestClient
    VERSION 1.0.0
    LANGUAGES CXX
)

# =============================================================================
# Project Configuration
# =============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Source Files
# =============================================================================

set(TESTCLIENT_SOURCES
    main.cpp
    src/TestClient.cpp
)

set(TESTCLIENT_HEADERS
    include/TestClient.h
    include/PlatformSocket.h
)

# =============================================================================
# Executable Target
# =============================================================================

add_executable(TestClient ${TESTCLIENT_SOURCES} ${TESTCLIENT_HEADERS})

# Include directories:
#   - Current source dir (for "include/TestClient.h", "include/PlatformSocket.h")
#   - ServerEngine dir (for "Utils/NetworkUtils.h", "Network/Core/PacketDefine.h", etc.)
target_include_directories(TestClient PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Server/ServerEngine
)

# =============================================================================
# Platform-Specific Configuration
# =============================================================================

if(MSVC)
    # English: MSVC compiler settings
    # 한글: MSVC 컴파일러 설정
    target_compile_definitions(TestClient PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
    target_compile_options(TestClient PRIVATE
        /W4          # Warning level 4
        /permissive- # Stricter conformance
    )
    target_link_libraries(TestClient PRIVATE
        ws2_32       # Winsock2
    )
elseif(APPLE)
    # English: macOS settings
    # 한글: macOS 설정
    target_compile_options(TestClient PRIVATE
        -Wall
        -Wextra
    )
    target_link_libraries(TestClient PRIVATE
        pthread
    )
else()
    # English: Linux/POSIX settings
    # 한글: Linux/POSIX 설정
    target_compile_options(TestClient PRIVATE
        -Wall
        -Wextra
    )
    target_link_libraries(TestClient PRIVATE
        pthread
    )
endif()

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "=== TestClient Configuration ===")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "================================")
message(STATUS "")
