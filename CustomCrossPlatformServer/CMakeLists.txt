cmake_minimum_required(VERSION 3.16)
project(CustomCrossPlatformServer LANGUAGES CXX)

# Use modern C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options to enable/disable specific backends
option(NET_ENABLE_IOCP "Enable IOCP backend" ON)
option(NET_ENABLE_RIO "Enable RIO backend" ON)
option(NET_ENABLE_EPOLL "Enable epoll backend" ON)
option(NET_ENABLE_IOURING "Enable io_uring backend" ON)

# Add the core library target
add_library(net
    src/core/engine.cpp
)

# Public include directories
target_include_directories(net PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# TODO: Add backend sources when implemented
# e.g. if(NET_ENABLE_IOCP)
#         target_sources(net PRIVATE src/backend/win_iocp/transport_iocp.cpp)
#      endif()
