cmake_minimum_required(VERSION 3.16)
project(MiniSQL VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

# --- GoogleTest (fetched automatically, no manual install needed) ---
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# Prevent overriding parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# --- Library: minisql_core (everything except main.cpp) ---
add_library(minisql_core
    src/lexer/Lexer.cpp
    src/lexer/Token.cpp
)
target_include_directories(minisql_core PUBLIC src)

# --- CLI executable ---
add_executable(minisql src/cli/main.cpp)
target_link_libraries(minisql PRIVATE minisql_core)

# --- Tests ---
add_executable(minisql_tests
    tests/lexer_test.cpp
)
target_link_libraries(minisql_tests PRIVATE minisql_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(minisql_tests)
