cmake_minimum_required(VERSION 3.20)
project(protogate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies
# Boost - using modern CMake config
find_package(Boost 1.74 REQUIRED)
# Note: We need Boost.Asio and Boost.Beast (header-only)

# JSON library
find_package(nlohmann_json 3.11 REQUIRED)

# YAML parser
find_package(yaml-cpp 0.7 REQUIRED)

# Logging library
find_package(spdlog 1.10 REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Subdirectories
add_subdirectory(src/common)
add_subdirectory(src/server)
add_subdirectory(src/agent)

# Tests (optional)
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
