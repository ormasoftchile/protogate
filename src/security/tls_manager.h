#pragma once

#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>

namespace protogate {
namespace security {

/**
 * @brief TLS context manager for server-side TLS operations
 * 
 * Responsibilities:
 * - Initialize OpenSSL TLS contexts for agent and client connections
 * - Load certificates and private keys from Azure Key Vault
 * - Support wildcard certificates for SNI routing
 * - Enforce TLS 1.2+ minimum version
 * - Support certificate hot reload without downtime
 */
class TLSManager {
public:
    using ssl_context = boost::asio::ssl::context;
    using ssl_context_ptr = std::shared_ptr<ssl_context>;

    /**
     * @brief Certificate information loaded from Key Vault
     */
    struct Certificate {
        std::string cert_pem;      // PEM-encoded certificate chain
        std::string key_pem;       // PEM-encoded private key
        std::string domain;        // Domain name or wildcard (*.tunnel.example.com)
        std::string key_vault_uri; // Key Vault secret URI for hot reload
    };

    /**
     * @brief Initialize TLS manager with Key Vault configuration
     * @param key_vault_uri Base URI of the Key Vault instance
     * @param io_context IO context for async operations (certificate reload timer)
     */
    explicit TLSManager(const std::string& key_vault_uri, 
                       boost::asio::io_context* io_context = nullptr);

    /**
     * @brief Load certificate from Key Vault by secret name
     * @param secret_name Name of the Key Vault secret containing PFX/PEM
     * @param domain Domain name this certificate is for (e.g., "*.tunnel.example.com")
     * @return true if certificate loaded successfully
     */
    bool load_certificate(const std::string& secret_name, const std::string& domain);

    /**
     * @brief Get or create TLS context for client-facing HTTPS server
     * @param sni_hostname SNI hostname from client (for wildcard routing)
     * @return SSL context for the given domain, or default context if no match
     */
    ssl_context_ptr get_client_context(const std::string& sni_hostname = "");

    /**
     * @brief Get TLS context for agent-facing TLS server
     * @return SSL context configured for agent authentication
     */
    ssl_context_ptr get_agent_context();

    /**
     * @brief Reload certificate from Key Vault (hot reload)
     * @param domain Domain name to reload certificate for
     * @return true if reload successful
     */
    bool reload_certificate(const std::string& domain);

    /**
     * @brief Reload all certificates from Key Vault
     * @return Number of certificates successfully reloaded
     */
    size_t reload_all_certificates();

    /**
     * @brief Start automatic certificate reload with specified interval
     * @param reload_interval_minutes Interval between reloads (default: 60 minutes)
     */
    void start_auto_reload(unsigned int reload_interval_minutes = 60);

    /**
     * @brief Stop automatic certificate reload
     */
    void stop_auto_reload();

    /**
     * @brief Check if auto-reload is running
     */
    bool is_auto_reload_active() const { return auto_reload_active_; }

private:
    /**
     * @brief Create SSL context from certificate data
     */
    ssl_context_ptr create_context(const Certificate& cert, bool require_client_cert = false);

    /**
     * @brief Configure SSL context with TLS 1.2+ enforcement
     */
    void configure_tls_options(ssl_context& ctx);

    /**
     * @brief Match SNI hostname to wildcard certificate domain
     */
    std::string match_certificate(const std::string& hostname) const;

    /**
     * @brief Schedule next certificate reload
     */
    void schedule_reload();

    std::string key_vault_uri_;
    std::unordered_map<std::string, Certificate> certificates_; // domain -> cert
    ssl_context_ptr default_client_context_;
    ssl_context_ptr agent_context_;
    std::shared_mutex mutex_; // Protect certificate map during hot reload
    
    // Auto-reload support
    boost::asio::io_context* io_context_;
    std::unique_ptr<boost::asio::steady_timer> reload_timer_;
    std::atomic<bool> auto_reload_active_;
    unsigned int reload_interval_minutes_;
};

}  // namespace security
}  // namespace protogate
