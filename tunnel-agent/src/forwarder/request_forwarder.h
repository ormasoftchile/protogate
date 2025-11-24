#pragma once

#include <string>
#include <map>
#include <vector>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include "../client/http2_session.h"

namespace protogate {
namespace agent {

struct ForwardResponse {
    int status_code;
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> body;
};

class RequestForwarder {
public:
    RequestForwarder(const std::string& local_url, int timeout_ms);
    ~RequestForwarder();
    
    ForwardResponse forward(const HTTP2Request& request);
    
private:
    std::string local_host_;
    std::string local_port_;
    std::string local_path_prefix_;
    int timeout_ms_;
    
    void parse_local_url(const std::string& url);
    ForwardResponse make_error_response(int status_code, const std::string& message);
};

}  // namespace agent
}  // namespace protogate
