cmake_minimum_required(VERSION 3.16)
project(DBModuleTest VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(SOURCES
    src/DatabaseFactory.cpp
    src/odbc/ODBCDatabase.cpp
)

# Header files
set(HEADERS
    include/IDatabase.h
    src/odbc/ODBCDatabase.h
)

# Create library
add_library(DBModuleTest STATIC ${SOURCES} ${HEADERS})

# ODBC libraries
if(WIN32)
    target_link_libraries(DBModuleTest odbc32 odbccp32)
    target_link_libraries(DBModuleTest ole32 oleaut32)
endif()

# Find ODBC package
find_package(ODBC QUIET)
if(ODBC_FOUND)
    target_link_libraries(DBModuleTest ${ODBC_LIBRARIES})
    target_include_directories(DBModuleTest PRIVATE ${ODBC_INCLUDE_DIRS})
endif()

# Compile options
if(MSVC)
    target_compile_options(DBModuleTest PRIVATE /W4 /WX)
else()
    target_compile_options(DBModuleTest PRIVATE -Wall -Wextra -Werror)
endif()

# Sample executables
add_executable(odbc_sample samples/odbc_sample.cpp)
target_link_libraries(odbc_sample DBModuleTest)

add_executable(oledb_sample samples/oledb_sample.cpp)
target_link_libraries(oledb_sample DBModuleTest)

# Test executable
add_executable(db_tests tests/test_database.cpp)
target_link_libraries(db_tests DBModuleTest)

# Installation
install(TARGETS DBModuleTest
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${HEADERS} DESTINATION include/DBModuleTest)