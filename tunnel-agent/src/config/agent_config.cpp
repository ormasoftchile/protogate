#include "agent_config.h"

namespace protogate {
namespace agent {

AgentConfig AgentConfig::from_json_file(const std::string& path) {
    // TODO: Implement JSON parsing
    throw std::runtime_error("Not implemented");
}

AgentConfig AgentConfig::from_cli_args(int argc, char* argv[]) {
    // TODO: Implement CLI parsing
    throw std::runtime_error("Not implemented");
}

AgentConfig AgentConfig::from_environment() {
    // TODO: Implement env var parsing
    throw std::runtime_error("Not implemented");
}

}  // namespace agent
}  // namespace protogate
