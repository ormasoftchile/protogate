#include "utils/config.h"
#include "utils/errors.h"
#include "observability/logger.h"
#include "server/io_context_pool.h"
#include "server/http_server.h"
#include "server/agent_server.h"
#include "security/tls_manager.h"
#include "security/token_validator.h"
#include "agent/agent_registry.h"
#include "proxy/http_proxy.h"
#include "storage/cache.h"
#include "models/tunnel.h"
#include "models/auth_token.h"
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
        auto io_pool = std::make_shared<server::IOContextPool>(config.io_thread_pool_size);
        
        LOG_INFO("IO context pool created", {
            {"thread_count", std::to_string(io_pool->size())}
        });
        
        // Initialize caches
        auto tunnel_cache = std::make_shared<storage::Cache<std::string, models::Tunnel>>();
        auto token_cache = std::make_shared<storage::Cache<std::string, models::AuthToken>>();
        
        LOG_INFO("Caches initialized");
        
        // Initialize TLS manager
        auto tls_manager = std::make_shared<security::TLSManager>(config.key_vault_uri);
        
        LOG_INFO("TLS manager initialized", {
            {"key_vault_uri", config.key_vault_uri}
        });
        
        // Initialize token validator
        auto token_validator = std::make_shared<security::TokenValidator>(token_cache);
        
        LOG_INFO("Token validator initialized");
        
        // Initialize agent registry
        auto agent_registry = std::make_shared<agent::AgentRegistry>(50);
        
        LOG_INFO("Agent registry initialized");
        
        // Initialize HTTP proxy
        auto http_proxy = std::make_shared<proxy::HTTPProxy>(tunnel_cache, agent_registry);
        
        LOG_INFO("HTTP proxy initialized");
        
        // Create servers
        auto http_server = std::make_shared<server::HTTPServer>(
            io_pool,
            tls_manager,
            http_proxy,
            config.port);
        
        auto agent_server = std::make_shared<server::AgentServer>(
            io_pool,
            tls_manager,
            token_validator,
            agent_registry,
            config.agent_port);
        
        LOG_INFO("Servers created");
        
        // Start servers
        http_server->start();
        agent_server->start();
        
        LOG_INFO("Servers started", {
            {"http_port", std::to_string(config.port)},
            {"agent_port", std::to_string(config.agent_port)}
        });
        
        // Start IO pool
        io_pool->start();
        
        LOG_INFO("Protogate server ready", {
            {"status", "listening"}
        });
        
        // Main loop - wait for shutdown signal
        while (!g_shutdown_requested) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        LOG_INFO("Shutdown signal received, stopping server");
        
        // Graceful shutdown
        http_server->stop();
        agent_server->stop();
        agent_registry->shutdown();
        io_pool->stop();
        
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
