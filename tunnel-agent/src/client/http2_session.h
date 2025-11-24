#pragma once

namespace protogate {
namespace agent {

class HTTP2Session {
public:
    HTTP2Session();
    ~HTTP2Session();
    
    void start();
    void stop();
    
private:
    bool active_;
};

}  // namespace agent
}  // namespace protogate
