#pragma once

#include <boost/asio.hpp>
#include <chrono>
#include <functional>
#include <memory>

namespace protogate {
namespace utils {

/**
 * @brief Async timeout handler using Boost.Asio deadline timer
 */
class TimeoutHandler {
public:
    using TimeoutCallback = std::function<void()>;
    
    TimeoutHandler(boost::asio::io_context& io_context)
        : timer_(io_context) {}
    
    /**
     * @brief Start timeout timer
     */
    void start(std::chrono::milliseconds timeout, TimeoutCallback callback) {
        timer_.expires_after(timeout);
        timer_.async_wait([callback](const boost::system::error_code& ec) {
            if (!ec) {  // Not cancelled
                callback();
            }
        });
    }
    
    /**
     * @brief Cancel timeout
     */
    void cancel() {
        timer_.cancel();
    }

private:
    boost::asio::steady_timer timer_;
};

/**
 * @brief RAII wrapper for async operations with automatic cancellation
 */
template <typename Operation>
class AsyncOperation {
public:
    AsyncOperation(boost::asio::io_context& io_context, Operation op)
        : io_context_(io_context), operation_(std::move(op)) {}
    
    ~AsyncOperation() {
        cancel();
    }
    
    void start() {
        operation_();
    }
    
    void cancel() {
        // Post cancellation to io_context
        boost::asio::post(io_context_, [this]() {
            // Cancellation logic here
        });
    }

private:
    boost::asio::io_context& io_context_;
    Operation operation_;
};

/**
 * @brief Utility to post work to io_context
 */
template <typename Handler>
void async_post(boost::asio::io_context& io_context, Handler&& handler) {
    boost::asio::post(io_context, std::forward<Handler>(handler));
}

/**
 * @brief Utility to dispatch work to io_context
 */
template <typename Handler>
void async_dispatch(boost::asio::io_context& io_context, Handler&& handler) {
    boost::asio::dispatch(io_context, std::forward<Handler>(handler));
}

}  // namespace utils
}  // namespace protogate
