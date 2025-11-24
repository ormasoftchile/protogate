#pragma once

#include <string>

namespace protogate {
namespace agent {

class Logger {
public:
    static void init(const std::string& level, const std::string& format);
    static void info(const std::string& message);
    static void error(const std::string& message);
};

}  // namespace agent
}  // namespace protogate
