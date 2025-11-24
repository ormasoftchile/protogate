cmake_minimum_required(VERSION 3.20)

# Remove vcpkg toolchain requirement - use system packages
if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(protogate-tunnel-agent VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    add_compile_options(/W4)
endif()

# Find packages
find_package(Boost REQUIRED COMPONENTS filesystem program_options)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NGHTTP2 REQUIRED libnghttp2)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

# Source files
set(AGENT_SOURCES
    src/config/agent_config.cpp
    src/client/tls_client.cpp
    src/client/http2_session.cpp
    src/forwarder/request_forwarder.cpp
    src/health/heartbeat.cpp
    src/utils/reconnect.cpp
    src/utils/logger.cpp
)

# Agent library
add_library(tunnel_agent_lib STATIC ${AGENT_SOURCES})

target_include_directories(tunnel_agent_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(tunnel_agent_lib PUBLIC
    Boost::filesystem
    Boost::program_options
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

target_include_directories(tunnel_agent_lib PUBLIC
    ${NGHTTP2_INCLUDE_DIRS}
)

target_link_directories(tunnel_agent_lib PUBLIC
    ${NGHTTP2_LIBRARY_DIRS}
)

target_link_libraries(tunnel_agent_lib PUBLIC
    ${NGHTTP2_LINK_LIBRARIES}
)

# Main executable
add_executable(tunnel-agent src/main.cpp)

target_link_libraries(tunnel-agent PRIVATE
    tunnel_agent_lib
)

# Tests (optional)
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Catch2 CONFIG)
    
    if(Catch2_FOUND)
        add_executable(agent_tests
            tests/test_config.cpp
            tests/test_client.cpp
            tests/test_forwarder.cpp
        )
        
        target_link_libraries(agent_tests PRIVATE
            tunnel_agent_lib
            Catch2::Catch2WithMain
        )
        
        add_test(NAME agent_tests COMMAND agent_tests)
    endif()
endif()

# Installation
install(TARGETS tunnel-agent
    RUNTIME DESTINATION bin
)

install(FILES config.example.json
    DESTINATION share/protogate-tunnel-agent
)
