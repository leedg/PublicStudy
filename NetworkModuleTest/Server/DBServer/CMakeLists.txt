cmake_minimum_required(VERSION 3.15)

# =============================================================================
# DBServer CMake
# =============================================================================

file(
    GLOB DBSERVER_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(TestDBServer
    main.cpp
    ${DBSERVER_SOURCES}
)

target_include_directories(TestDBServer
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../ServerEngine
)

target_link_libraries(TestDBServer PRIVATE ServerEngine)

if (ENABLE_DATABASE_SUPPORT)
    target_compile_definitions(TestDBServer PRIVATE ENABLE_DATABASE_SUPPORT=1)
endif()
