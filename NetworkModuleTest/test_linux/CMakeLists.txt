# =============================================================================
# NetworkModuleLinuxTest — Root CMake (Linux-only build)
# =============================================================================
# English: Builds ServerEngine (static lib) + TestServer + DBServer + TestClient
#          Run from test_linux/scripts/build.sh inside Docker, or manually:
#            cmake -B /workspace/build/linux -S /workspace/test_linux -GNinja
#            cmake --build /workspace/build/linux -- -j$(nproc)
# 한글: ServerEngine (정적 라이브러리) + TestServer + DBServer + TestClient 빌드
#       Docker 내부에서 test_linux/scripts/build.sh로 실행하거나 직접 실행 가능

cmake_minimum_required(VERSION 3.20)
project(NetworkModuleLinuxTest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Paths
# =============================================================================

set(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Server/ServerEngine")

# =============================================================================
# io_uring option
# =============================================================================

option(ENABLE_IO_URING "Enable io_uring backend (Linux)" ON)

if(ENABLE_IO_URING)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBURING liburing)
    if(LIBURING_FOUND)
        message(STATUS "liburing found (${LIBURING_VERSION}) — io_uring backend enabled")
        set(HAVE_IO_URING ON)
    else()
        # English: Manual fallback search
        # 한글: 수동 폴백 검색
        find_library(LIBURING_LIB NAMES uring liburing)
        find_path(LIBURING_INCLUDE NAMES liburing.h)
        if(LIBURING_LIB AND LIBURING_INCLUDE)
            message(STATUS "liburing found manually: ${LIBURING_LIB}")
            set(HAVE_IO_URING ON)
            set(LIBURING_LIBRARIES ${LIBURING_LIB})
            set(LIBURING_INCLUDE_DIRS ${LIBURING_INCLUDE})
        else()
            message(WARNING "liburing not found — io_uring disabled, epoll only")
            set(HAVE_IO_URING OFF)
            set(ENABLE_IO_URING OFF)
        endif()
    endif()
else()
    message(STATUS "io_uring backend disabled (ENABLE_IO_URING=OFF)")
    set(HAVE_IO_URING OFF)
endif()

# =============================================================================
# SQLite3 option
# =============================================================================

find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 sqlite3)
if(SQLITE3_FOUND)
    message(STATUS "SQLite3 found — SQLiteDatabase enabled")
    set(HAVE_SQLITE3 ON)
else()
    message(WARNING "SQLite3 not found — SQLiteDatabase will be a no-op")
    set(HAVE_SQLITE3 OFF)
endif()

# =============================================================================
# ServerEngine static library
# =============================================================================

add_library(ServerEngine STATIC)

target_sources(ServerEngine PRIVATE
    # -------------------------------------------------------------------------
    # Concurrency (most are header-only; TimerQueue has a .cpp)
    # -------------------------------------------------------------------------
    ${ENGINE_ROOT}/Concurrency/TimerQueue.cpp

    # -------------------------------------------------------------------------
    # Core/Memory
    # -------------------------------------------------------------------------
    ${ENGINE_ROOT}/Core/Memory/StandardBufferPool.cpp
    ${ENGINE_ROOT}/Core/Memory/IOUringBufferPool.cpp     # guarded by #ifdef __linux__ internally

    # -------------------------------------------------------------------------
    # Network/Core
    # -------------------------------------------------------------------------
    ${ENGINE_ROOT}/Network/Core/AsyncIOProvider.cpp
    ${ENGINE_ROOT}/Network/Core/BaseNetworkEngine.cpp
    ${ENGINE_ROOT}/Network/Core/NetworkEngineFactory.cpp
    ${ENGINE_ROOT}/Network/Core/NetworkEventBus.cpp
    ${ENGINE_ROOT}/Network/Core/PlatformDetect.cpp
    ${ENGINE_ROOT}/Network/Core/SendBufferPool.cpp
    ${ENGINE_ROOT}/Network/Core/Session.cpp
    ${ENGINE_ROOT}/Network/Core/SessionManager.cpp
    ${ENGINE_ROOT}/Network/Core/SessionPool.cpp

    # -------------------------------------------------------------------------
    # Network/Platforms — Linux engine
    # -------------------------------------------------------------------------
    ${ENGINE_ROOT}/Network/Platforms/LinuxNetworkEngine.cpp

    # -------------------------------------------------------------------------
    # AsyncIO/Platforms — Linux providers
    # -------------------------------------------------------------------------
    ${ENGINE_ROOT}/Platforms/Linux/EpollAsyncIOProvider.cpp
    ${ENGINE_ROOT}/Platforms/Linux/IOUringAsyncIOProvider.cpp

    # -------------------------------------------------------------------------
    # Database
    # -------------------------------------------------------------------------
    ${ENGINE_ROOT}/Database/DatabaseFactory.cpp
    ${ENGINE_ROOT}/Database/MockDatabase.cpp
    ${ENGINE_ROOT}/Database/SQLiteDatabase.cpp
    ${ENGINE_ROOT}/Database/ConnectionPool.cpp
)

# English: ServerEngine include root — lets sources use "Network/Core/...", "Utils/..." etc.
# 한글: ServerEngine 인클루드 루트 — "Network/Core/...", "Utils/..." 등 경로 사용
target_include_directories(ServerEngine PUBLIC ${ENGINE_ROOT})

# io_uring definitions and link
if(HAVE_IO_URING)
    target_compile_definitions(ServerEngine PUBLIC HAVE_IO_URING HAVE_LIBURING)
    target_include_directories(ServerEngine PUBLIC ${LIBURING_INCLUDE_DIRS})
    target_link_libraries(ServerEngine PUBLIC ${LIBURING_LIBRARIES})
endif()

# SQLite3 definitions and link
if(HAVE_SQLITE3)
    target_compile_definitions(ServerEngine PUBLIC HAVE_SQLITE3)
    target_include_directories(ServerEngine PUBLIC ${SQLITE3_INCLUDE_DIRS})
    target_link_libraries(ServerEngine PUBLIC ${SQLITE3_LIBRARIES})
endif()

# POSIX threads
target_link_libraries(ServerEngine PUBLIC pthread)

target_compile_options(ServerEngine PRIVATE
    -Wall
    -Wextra
    -fPIC
    # English: Suppress noisy warnings from headers we cannot modify
    # 한글: 수정 불가한 헤더의 노이즈 경고 억제
    -Wno-unused-parameter
)

# =============================================================================
# TestServer executable
# =============================================================================

set(TESTSERVER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Server/TestServer")

add_executable(TestServer
    ${TESTSERVER_ROOT}/main.cpp
    ${TESTSERVER_ROOT}/src/TestServer.cpp
    ${TESTSERVER_ROOT}/src/ClientPacketHandler.cpp
    ${TESTSERVER_ROOT}/src/ClientSession.cpp
    ${TESTSERVER_ROOT}/src/DBServerPacketHandler.cpp
    ${TESTSERVER_ROOT}/src/DBServerSession.cpp
    ${TESTSERVER_ROOT}/src/DBTaskQueue.cpp
    ${TESTSERVER_ROOT}/src/ServerSession.cpp
    ${TESTSERVER_ROOT}/src/TestDatabaseManager.cpp
    ${TESTSERVER_ROOT}/src/TestMessageHandler.cpp
)

target_include_directories(TestServer PRIVATE
    ${TESTSERVER_ROOT}/include
    ${ENGINE_ROOT}
)

target_link_libraries(TestServer PRIVATE ServerEngine)

target_compile_options(TestServer PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# =============================================================================
# DBServer executable
# =============================================================================

set(DBSERVER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Server/DBServer")

add_executable(DBServer
    ${DBSERVER_ROOT}/main.cpp
    ${DBSERVER_ROOT}/src/DBPingTimeManager.cpp
    ${DBSERVER_ROOT}/src/DBServer.cpp
    ${DBSERVER_ROOT}/src/OrderedTaskQueue.cpp
    ${DBSERVER_ROOT}/src/ServerLatencyManager.cpp
    ${DBSERVER_ROOT}/src/ServerPacketHandler.cpp
    ${DBSERVER_ROOT}/src/TestDBServer.cpp
)

target_include_directories(DBServer PRIVATE
    ${DBSERVER_ROOT}/include
    ${ENGINE_ROOT}
)

target_link_libraries(DBServer PRIVATE ServerEngine)

target_compile_options(DBServer PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# =============================================================================
# TestClient executable (uses its own CMakeLists.txt)
# =============================================================================

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../Client/TestClient
    ${CMAKE_CURRENT_BINARY_DIR}/TestClient
)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "=== NetworkModuleLinuxTest Configuration ===")
message(STATUS "Platform        : ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler        : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ Standard    : ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type      : ${CMAKE_BUILD_TYPE}")
message(STATUS "io_uring        : ${HAVE_IO_URING}")
message(STATUS "SQLite3         : ${HAVE_SQLITE3}")
message(STATUS "============================================")
message(STATUS "")
