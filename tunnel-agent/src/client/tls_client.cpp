#include "tls_client.h"

namespace protogate {
namespace agent {

TLSClient::TLSClient(const std::string& host, unsigned short port, bool verify_tls)
    : host_(host), port_(port), verify_tls_(verify_tls), connected_(false) {
}

TLSClient::~TLSClient() {
    disconnect();
}

void TLSClient::connect() {
    // TODO: Implement TLS connection
}

void TLSClient::disconnect() {
    connected_ = false;
}

bool TLSClient::is_connected() const {
    return connected_;
}

}  // namespace agent
}  // namespace protogate
