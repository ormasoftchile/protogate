# ProtoGate Agent executable

add_executable(protogate-agent
    main.cpp
)

target_link_libraries(protogate-agent
    PRIVATE
    protogate-common
    Boost::boost
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    spdlog::spdlog
)

target_include_directories(protogate-agent
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
