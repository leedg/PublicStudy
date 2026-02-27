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
