# =============================================================================
# NetworkModule Root CMakeLists.txt
# =============================================================================
# Root build configuration for the NetworkModule project

cmake_minimum_required(VERSION 3.15)

project(
    NetworkModule
    VERSION 1.0.0
    LANGUAGES CXX
)

# =============================================================================
# Project-wide Configuration
# =============================================================================

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# =============================================================================
# Subdirectories

# AsyncIO Library
add_subdirectory(ModuleTest/MultiPlatformNetwork)

# TestClient - Cross-platform TCP test client
add_subdirectory(Client/TestClient)

# =============================================================================
# Server Targets (Cross-platform)
# =============================================================================

option(BUILD_SERVER_ENGINE "Build ServerEngine library" ON)
option(BUILD_TEST_SERVER "Build TestServer" ON)
option(BUILD_DB_SERVER "Build DBServer" ON)
option(ENABLE_DATABASE_SUPPORT "Enable database support" OFF)

if (BUILD_TEST_SERVER AND NOT BUILD_SERVER_ENGINE)
    message(FATAL_ERROR "TestServer requires BUILD_SERVER_ENGINE=ON")
endif()

if (BUILD_DB_SERVER AND NOT BUILD_SERVER_ENGINE)
    message(FATAL_ERROR "DBServer requires BUILD_SERVER_ENGINE=ON")
endif()

if (BUILD_SERVER_ENGINE)
    add_subdirectory(Server/ServerEngine)
endif()

if (BUILD_TEST_SERVER)
    add_subdirectory(Server/TestServer)
endif()

if (BUILD_DB_SERVER)
    add_subdirectory(Server/DBServer)
endif()

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "========== NetworkModule Configuration ==========")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Binary Directory: ${CMAKE_BINARY_DIR}")
message(STATUS "Source Directory: ${CMAKE_SOURCE_DIR}")
message(STATUS "================================================")
message(STATUS "")
