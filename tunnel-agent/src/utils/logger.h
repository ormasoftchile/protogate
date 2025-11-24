#pragma once

#include <string>
#include <map>
#include <memory>

namespace spdlog {
    class logger;
}

namespace protogate {
namespace agent {

class Logger {
public:
    static void init(const std::string& level, const std::string& format);
    static void info(const std::string& message, const std::map<std::string, std::string>& fields = {});
    static void error(const std::string& message, const std::map<std::string, std::string>& fields = {});
    static void warning(const std::string& message, const std::map<std::string, std::string>& fields = {});
    static void debug(const std::string& message, const std::map<std::string, std::string>& fields = {});
    
private:
    static std::shared_ptr<spdlog::logger> logger_;
    static std::string format_;
};

}  // namespace agent
}  // namespace protogate
