#include "utils/config.h"
#include "utils/errors.h"
#include "observability/logger.h"
#include "server/io_context_pool.h"
#include <iostream>
#include <csignal>
#include <atomic>

namespace {
std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signal) {
    if (signal == SIGTERM || signal == SIGINT) {
        g_shutdown_requested = true;
    }
}
}  // namespace

int main(int argc, char* argv[]) {
    using namespace protogate;
    
    try {
        // Load configuration from environment
        auto config = utils::Config::from_environment();
        
        // Initialize logger
        observability::Logger::initialize(
            observability::LogLevel::INFO,
            config.log_analytics_workspace_id.value_or(""),
            config.log_analytics_key.value_or(""));
        
        LOG_INFO("Protogate server starting", {
            {"version", "1.0.0"},
            {"port", std::to_string(config.port)},
            {"agent_port", std::to_string(config.agent_port)},
            {"dns_zone", config.dns_zone}
        });
        
        // Setup signal handlers
        std::signal(SIGTERM, signal_handler);
        std::signal(SIGINT, signal_handler);
        
        // Create IO context pool
        server::IOContextPool io_pool(config.io_thread_pool_size);
        
        LOG_INFO("IO context pool created", {
            {"thread_count", std::to_string(io_pool.size())}
        });
        
        // TODO: Initialize server components
        // - TLS manager
        // - Token validator
        // - Agent registry
        // - HTTP server
        // - Agent server
        // - TCP server (if configured)
        
        // Start IO pool
        io_pool.start();
        
        LOG_INFO("Protogate server ready", {
            {"status", "listening"}
        });
        
        // Main loop - wait for shutdown signal
        while (!g_shutdown_requested) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        LOG_INFO("Shutdown signal received, stopping server");
        
        // Graceful shutdown
        io_pool.stop();
        
        LOG_INFO("Protogate server stopped");
        
        return 0;
        
    } catch (const errors::ProtogateException& e) {
        std::cerr << "Protogate error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
