cmake_minimum_required(VERSION 3.20)
project(protogate VERSION 1.0.0 LANGUAGES CXX)

# Set C++17 as minimum standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-unused-parameter
    )
    
    # Debug flags
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0 -fsanitize=address -fsanitize=undefined)
        add_link_options(-fsanitize=address -fsanitize=undefined)
    else()
        add_compile_options(-O3 -DNDEBUG)
    endif()
endif()

# vcpkg integration
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "")
endif()

# Find required packages
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)

# Optional packages for full build
find_package(GTest)
find_package(benchmark)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# Source files for main server
file(GLOB_RECURSE PROTOGATE_SOURCES
    src/server/*.cpp
    src/agent/*.cpp
    src/proxy/*.cpp
    src/security/*.cpp
    src/storage/*.cpp
    src/observability/*.cpp
    src/models/*.cpp
    src/utils/*.cpp
    src/api/*.cpp
)

# Remove main.cpp from library sources
list(FILTER PROTOGATE_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# Create library for shared code
add_library(protogate_lib STATIC ${PROTOGATE_SOURCES})
target_link_libraries(protogate_lib PUBLIC
    Boost::system
    Boost::thread
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    pthread
)

# Main executable
add_executable(protogate-server src/server/main.cpp)
target_link_libraries(protogate-server PRIVATE protogate_lib)

# Install targets
install(TARGETS protogate-server
    RUNTIME DESTINATION bin
)

# Testing
if(GTest_FOUND AND BUILD_TESTING)
    enable_testing()
    
    # Unit tests
    file(GLOB_RECURSE UNIT_TEST_SOURCES tests/unit/*.cpp)
    if(UNIT_TEST_SOURCES)
        add_executable(unit_tests ${UNIT_TEST_SOURCES})
        target_link_libraries(unit_tests PRIVATE
            protogate_lib
            GTest::gtest
            GTest::gtest_main
        )
        add_test(NAME unit_tests COMMAND unit_tests)
    endif()
    
    # Integration tests
    file(GLOB_RECURSE INTEGRATION_TEST_SOURCES tests/integration/*.cpp)
    if(INTEGRATION_TEST_SOURCES)
        add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
        target_link_libraries(integration_tests PRIVATE
            protogate_lib
            GTest::gtest
            GTest::gtest_main
        )
        add_test(NAME integration_tests COMMAND integration_tests)
    endif()
endif()

# Performance benchmarks
if(benchmark_FOUND)
    file(GLOB_RECURSE BENCHMARK_SOURCES tests/performance/*.cpp)
    if(BENCHMARK_SOURCES)
        add_executable(benchmarks ${BENCHMARK_SOURCES})
        target_link_libraries(benchmarks PRIVATE
            protogate_lib
            benchmark::benchmark
            benchmark::benchmark_main
        )
    endif()
endif()

# Print configuration summary
message(STATUS "Protogate Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
