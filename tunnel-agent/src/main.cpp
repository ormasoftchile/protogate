#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include "config/agent_config.h"
#include "utils/logger.h"
#include "client/tls_client.h"
#include "client/http2_session.h"
#include "forwarder/request_forwarder.h"
#include "health/heartbeat.h"
#include "utils/reconnect.h"

std::atomic<bool> running{true};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        protogate::agent::Logger::info("Received shutdown signal");
        running = false;
    }
}

void run_agent(const protogate::agent::AgentConfig& config) {
    using namespace protogate::agent;
    
    ReconnectionManager reconnect(1000, 60000);  // 1s initial, 60s max
    
    while (running && reconnect.should_reconnect()) {
        try {
            // Create TLS client
            TLSClient tls_client(config.server.host, config.server.port, config.server.verify_tls);
            
            Logger::info("Connecting to server", {
                {"host", config.server.host},
                {"port", std::to_string(config.server.port)}
            });
            
            tls_client.connect();
            
            // Create HTTP/2 session
            HTTP2Session http2_session(tls_client, config.tunnel.id, config.tunnel.token);
            
            // Create request forwarder
            RequestForwarder forwarder(config.local.url, config.local.timeout_seconds * 1000);
            
            // Create heartbeat manager
            HeartbeatManager heartbeat(http2_session, 
                                      config.health.heartbeat_interval_seconds * 1000,
                                      config.health.heartbeat_timeout_seconds * 1000);
            
            // Start HTTP/2 session with request callback
            http2_session.start([&](const HTTP2Request& request) {
                Logger::info("Received request", {
                    {"stream_id", std::to_string(request.stream_id)},
                    {"method", request.method},
                    {"path", request.path},
                    {"request_id", request.request_id}
                });
                
                // Forward to local service
                auto response = forwarder.forward(request);
                
                // Send response back
                http2_session.send_response(request.stream_id, response.status_code,
                                           response.headers, response.body);
            });
            
            heartbeat.start();
            
            Logger::info("Agent connected and ready");
            
            // Reset reconnection delay on successful connection
            reconnect.reset();
            
            // Main event loop
            while (running && http2_session.is_active()) {
                // Process HTTP/2 events
                http2_session.process_events();
                
                // Send heartbeat if needed
                if (heartbeat.should_send_ping()) {
                    http2_session.send_ping();
                }
                
                // Check heartbeat timeout
                if (heartbeat.is_timed_out()) {
                    Logger::error("Heartbeat timeout, disconnecting");
                    break;
                }
                
                // Small delay to avoid busy loop
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            Logger::info("Session ended");
            
            heartbeat.stop();
            http2_session.stop();
            tls_client.disconnect();
            
        } catch (const std::exception& e) {
            Logger::error("Connection error", {
                {"error", e.what()}
            });
        }
        
        if (running && reconnect.should_reconnect()) {
            int delay_ms = reconnect.get_next_delay_ms();
            
            Logger::info("Reconnecting", {
                {"delay_ms", std::to_string(delay_ms)}
            });
            
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
    
    Logger::info("Agent stopped");
}

int main(int argc, char* argv[]) {
    using namespace protogate::agent;
    
    try {
        // Handle --help
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
                std::cout << "Protogate Tunnel Agent v1.0.0\n\n";
                std::cout << "Usage:\n";
                std::cout << "  tunnel-agent [options]\n\n";
                std::cout << "Options:\n";
                std::cout << "  --config <file>      Configuration file path\n";
                std::cout << "  --server <host:port> Server address\n";
                std::cout << "  --token <token>      Tunnel authentication token\n";
                std::cout << "  --tunnel-id <id>     Tunnel ID\n";
                std::cout << "  --local-url <url>    Local service URL\n";
                std::cout << "  --help               Show this help\n\n";
                std::cout << "Environment variables:\n";
                std::cout << "  TUNNEL_SERVER        Server address\n";
                std::cout << "  TUNNEL_TOKEN         Authentication token\n";
                std::cout << "  TUNNEL_ID            Tunnel ID\n";
                std::cout << "  LOCAL_URL            Local service URL\n";
                return 0;
            }
        }
        
        // Load configuration
        AgentConfig config;
        
        // Try CLI args first
        try {
            config = AgentConfig::from_cli_args(argc, argv);
        } catch (...) {
            // Try environment variables
            try {
                config = AgentConfig::from_environment();
            } catch (...) {
                // Try default config file
                config = AgentConfig::from_json_file("config.json");
            }
        }
        
        // Validate configuration
        config.validate();
        
        // Initialize logger
        Logger::init(config.logging.level, config.logging.format);
        
        Logger::info("Starting Protogate Tunnel Agent", {
            {"version", "1.0.0"},
            {"tunnel_id", config.tunnel.id},
            {"server", config.server.host + ":" + std::to_string(config.server.port)},
            {"local_url", config.local.url}
        });
        
        // Setup signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Run agent
        run_agent(config);
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
