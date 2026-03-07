cmake_minimum_required(VERSION 3.15)

# =============================================================================
# TestServer CMake
# =============================================================================

file(
    GLOB TESTSERVER_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(TestServer
    main.cpp
    ${TESTSERVER_SOURCES}
)

target_include_directories(TestServer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../ServerEngine
)

target_link_libraries(TestServer PRIVATE ServerEngine)

if (ENABLE_DATABASE_SUPPORT)
    target_compile_definitions(TestServer PRIVATE ENABLE_DATABASE_SUPPORT=1)
endif()
