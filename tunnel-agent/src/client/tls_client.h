#pragma once

#include <string>
#include <memory>

namespace protogate {
namespace agent {

class TLSClient {
public:
    TLSClient(const std::string& host, unsigned short port, bool verify_tls);
    ~TLSClient();
    
    void connect();
    void disconnect();
    bool is_connected() const;
    
private:
    std::string host_;
    unsigned short port_;
    bool verify_tls_;
    bool connected_;
};

}  // namespace agent
}  // namespace protogate
