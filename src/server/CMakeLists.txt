# ProtoGate Server executable

add_executable(protogate-server
    main.cpp
)

target_link_libraries(protogate-server
    PRIVATE
    protogate-common
    Boost::boost
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    spdlog::spdlog
)

target_include_directories(protogate-server
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
