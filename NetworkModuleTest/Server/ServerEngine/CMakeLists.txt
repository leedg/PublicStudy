cmake_minimum_required(VERSION 3.15)

# =============================================================================
# ServerEngine CMake
# =============================================================================
# Cross-platform static library for the current ServerEngine source layout.

option(ENABLE_IO_URING "Enable io_uring backend when liburing is available" OFF)
option(ENABLE_DATABASE_SUPPORT "Enable Windows ODBC/OLEDB database backends" OFF)
option(ENABLE_ASYNCIO_TESTS "Build the AsyncIO smoke test executable" OFF)

file(
    GLOB_RECURSE SERVER_ENGINE_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Concurrency/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/Memory/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Database/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Implementations/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Network/Core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Protocols/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Utils/*.cpp"
)

# Sample snippets and legacy files are not part of the library build.
list(FILTER SERVER_ENGINE_SOURCES EXCLUDE REGEX "/Database/Examples/")
list(FILTER SERVER_ENGINE_SOURCES EXCLUDE REGEX "/Database/ConnectionPool.cpp$")
list(FILTER SERVER_ENGINE_SOURCES EXCLUDE REGEX "/Database/ODBCDatabase.cpp$")
list(FILTER SERVER_ENGINE_SOURCES EXCLUDE REGEX "/Database/OLEDBDatabase.cpp$")
list(FILTER SERVER_ENGINE_SOURCES EXCLUDE REGEX "/Core/Memory/IOUringBufferPool.cpp$")
list(FILTER SERVER_ENGINE_SOURCES EXCLUDE REGEX "/Core/Memory/RIOBufferPool.cpp$")
list(FILTER SERVER_ENGINE_SOURCES EXCLUDE REGEX "/Network/Core/AsyncIONetworkEngine.cpp$")
list(FILTER SERVER_ENGINE_SOURCES EXCLUDE REGEX "/Network/Core/IOCPNetworkEngine.cpp$")

set(SERVER_ENGINE_PLATFORM_SOURCES)

if (WIN32)
    list(APPEND SERVER_ENGINE_PLATFORM_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Network/Platforms/WindowsNetworkEngine.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Platforms/Windows/IocpAsyncIOProvider.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Platforms/Windows/RIOAsyncIOProvider.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Memory/RIOBufferPool.cpp"
    )
elseif (APPLE)
    list(APPEND SERVER_ENGINE_PLATFORM_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Network/Platforms/macOSNetworkEngine.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Platforms/macOS/KqueueAsyncIOProvider.cpp"
    )
elseif (UNIX)
    list(APPEND SERVER_ENGINE_PLATFORM_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Network/Platforms/LinuxNetworkEngine.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Platforms/Linux/EpollAsyncIOProvider.cpp"
    )
else()
    message(FATAL_ERROR "Unsupported platform for ServerEngine")
endif()

set(SERVER_ENGINE_PRIVATE_DEFS)
set(SERVER_ENGINE_PRIVATE_LIBS)

if (UNIX AND NOT APPLE)
    if (ENABLE_IO_URING)
        find_package(PkgConfig QUIET)
        if (PkgConfig_FOUND)
            pkg_check_modules(LIBURING QUIET liburing)
        endif()

        if (LIBURING_FOUND)
            list(APPEND SERVER_ENGINE_PLATFORM_SOURCES
                "${CMAKE_CURRENT_SOURCE_DIR}/Platforms/Linux/IOUringAsyncIOProvider.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/Core/Memory/IOUringBufferPool.cpp"
            )
            list(APPEND SERVER_ENGINE_PRIVATE_DEFS NETWORK_ENABLE_IO_URING=1)
            list(APPEND SERVER_ENGINE_PRIVATE_LIBS ${LIBURING_LIBRARIES})
        else()
            message(WARNING "liburing not found; building epoll-only ServerEngine")
        endif()
    endif()
endif()

if (WIN32 AND ENABLE_DATABASE_SUPPORT)
    list(APPEND SERVER_ENGINE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Database/ConnectionPool.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Database/ODBCDatabase.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Database/OLEDBDatabase.cpp"
    )
    list(APPEND SERVER_ENGINE_PRIVATE_DEFS ENABLE_DATABASE_SUPPORT=1)
endif()

add_library(ServerEngine STATIC
    ${SERVER_ENGINE_SOURCES}
    ${SERVER_ENGINE_PLATFORM_SOURCES}
)

target_include_directories(ServerEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if (SERVER_ENGINE_PRIVATE_DEFS)
    target_compile_definitions(ServerEngine PRIVATE ${SERVER_ENGINE_PRIVATE_DEFS})
endif()

find_package(Threads REQUIRED)
target_link_libraries(ServerEngine PUBLIC Threads::Threads)

if (WIN32)
    target_compile_definitions(ServerEngine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(ServerEngine PUBLIC ws2_32 mswsock)
    if (ENABLE_DATABASE_SUPPORT)
        target_link_libraries(ServerEngine PRIVATE ole32 oleaut32)
    endif()
endif()

if (UNIX AND NOT APPLE AND LIBURING_FOUND)
    target_include_directories(ServerEngine PRIVATE ${LIBURING_INCLUDE_DIRS})
    target_link_libraries(ServerEngine PRIVATE ${SERVER_ENGINE_PRIVATE_LIBS})
endif()

find_package(SQLite3 QUIET)
if (SQLite3_FOUND)
    target_compile_definitions(ServerEngine PUBLIC HAVE_SQLITE3=1)
    target_link_libraries(ServerEngine PUBLIC SQLite::SQLite3)
endif()

if (ENABLE_ASYNCIO_TESTS)
    add_executable(ServerEngineAsyncIOTest
        Tests/AsyncIOTest.cpp
    )
    target_link_libraries(ServerEngineAsyncIOTest PRIVATE ServerEngine)
endif()
