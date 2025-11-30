# ProtoGate Common Library

add_library(protogate-common STATIC
    config.cpp
    logger.cpp
    protocol.cpp
)

target_link_libraries(protogate-common
    PUBLIC
    Boost::boost
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    spdlog::spdlog
)

target_include_directories(protogate-common
    PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
