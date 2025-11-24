#include "http2_session.h"

namespace protogate {
namespace agent {

HTTP2Session::HTTP2Session() : active_(false) {
}

HTTP2Session::~HTTP2Session() {
    stop();
}

void HTTP2Session::start() {
    // TODO: Implement HTTP/2 session
}

void HTTP2Session::stop() {
    active_ = false;
}

}  // namespace agent
}  // namespace protogate
