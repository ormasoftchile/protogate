#include "server/io_context_pool.h"
#include <thread>

namespace protogate {
namespace server {

IOContextPool::IOContextPool(size_t pool_size) {
    if (pool_size == 0) {
        pool_size = std::thread::hardware_concurrency();
        if (pool_size == 0) {
            pool_size = 2;  // Fallback if detection fails
        }
    }
    
    // Create io_context instances
    for (size_t i = 0; i < pool_size; ++i) {
        auto io_context = std::make_shared<boost::asio::io_context>();
        auto work = boost::asio::make_work_guard(*io_context);
        
        io_contexts_.push_back(io_context);
        work_guards_.push_back(work);
    }
}

IOContextPool::~IOContextPool() {
    stop();
}

void IOContextPool::start() {
    // Create thread for each io_context
    for (auto& io_context : io_contexts_) {
        threads_.emplace_back([io_context]() {
            io_context->run();
        });
    }
}

void IOContextPool::stop() {
    // Remove work guards to allow io_context to finish
    work_guards_.clear();
    
    // Stop all io_contexts
    for (auto& io_context : io_contexts_) {
        io_context->stop();
    }
    
    // Join all threads
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    threads_.clear();
}

boost::asio::io_context& IOContextPool::get_io_context() {
    auto& io_context = *io_contexts_[next_io_context_];
    next_io_context_ = (next_io_context_ + 1) % io_contexts_.size();
    return io_context;
}

}  // namespace server
}  // namespace protogate
