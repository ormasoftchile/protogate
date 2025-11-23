#pragma once

#include <boost/asio.hpp>
#include <vector>
#include <thread>
#include <memory>

namespace protogate {
namespace server {

/**
 * @brief Thread pool of Boost.Asio io_context instances
 * 
 * Provides one io_context per CPU core for optimal performance
 */
class IOContextPool {
public:
    /**
     * @brief Initialize pool with specified number of threads
     * @param pool_size Number of threads (0 = auto-detect CPU cores)
     */
    explicit IOContextPool(size_t pool_size = 0);
    
    ~IOContextPool();
    
    // Non-copyable, non-movable
    IOContextPool(const IOContextPool&) = delete;
    IOContextPool& operator=(const IOContextPool&) = delete;
    
    /**
     * @brief Start all io_context threads
     */
    void start();
    
    /**
     * @brief Stop all io_context threads
     */
    void stop();
    
    /**
     * @brief Get an io_context for use (round-robin)
     */
    boost::asio::io_context& get_io_context();
    
    /**
     * @brief Get number of io_context instances
     */
    size_t size() const { return io_contexts_.size(); }

private:
    std::vector<std::shared_ptr<boost::asio::io_context>> io_contexts_;
    std::vector<std::shared_ptr<boost::asio::io_context::work>> work_guards_;
    std::vector<std::thread> threads_;
    size_t next_io_context_ = 0;
};

}  // namespace server
}  // namespace protogate
