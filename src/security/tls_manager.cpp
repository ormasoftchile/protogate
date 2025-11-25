#include "tls_manager.h"
#include "../observability/logger.h"
#include "../storage/keyvault_client.h"
#include <boost/asio/ssl/context.hpp>
#include <shared_mutex>
#include <algorithm>
#include <fstream>
#include <filesystem>

namespace protogate {
namespace security {

TLSManager::TLSManager(const std::string& key_vault_uri, boost::asio::io_context* io_context)
    : key_vault_uri_(key_vault_uri),
      io_context_(io_context),
      auto_reload_active_(false),
      reload_interval_minutes_(60) {
    
    // Create default contexts
    default_client_context_ = std::make_shared<ssl_context>(ssl_context::tlsv12_server);
    agent_context_ = std::make_shared<ssl_context>(ssl_context::tlsv12_server);
    
    configure_tls_options(*default_client_context_);
    configure_tls_options(*agent_context_);
    
    // For development with mock Key Vault, try to load local certificates
    if (key_vault_uri.find("mock") != std::string::npos) {
        load_local_development_cert();
    }
    
    observability::Logger::instance().info("TLSManager initialized", {
        {"key_vault_uri", key_vault_uri_},
        {"auto_reload_support", io_context_ ? "enabled" : "disabled"}
    });
}

void TLSManager::load_local_development_cert() {
    try {
        // Try to load localhost.crt and localhost.key from current directory
        std::filesystem::path cert_path = "localhost.crt";
        std::filesystem::path key_path = "localhost.key";
        
        if (!std::filesystem::exists(cert_path) || !std::filesystem::exists(key_path)) {
            observability::Logger::instance().warning("Local development certificates not found", {
                {"cert_path", cert_path.string()},
                {"key_path", key_path.string()}
            });
            return;
        }
        
        // Load certificate and key
        default_client_context_->use_certificate_chain_file(cert_path.string());
        default_client_context_->use_private_key_file(key_path.string(), ssl_context::pem);
        agent_context_->use_certificate_chain_file(cert_path.string());
        agent_context_->use_private_key_file(key_path.string(), ssl_context::pem);
        
        observability::Logger::instance().info("Loaded local development certificates", {
            {"cert_path", cert_path.string()},
            {"key_path", key_path.string()}
        });
        
    } catch (const std::exception& e) {
        observability::Logger::instance().warning("Failed to load local development certificates", {
            {"error", e.what()}
        });
    }
}

bool TLSManager::load_certificate(const std::string& secret_name, const std::string& domain) {
    try {
        // Load certificate from Key Vault
        storage::KeyVaultClient kv_client(key_vault_uri_);
        auto cert_data = kv_client.get_certificate(secret_name);
        
        if (!cert_data) {
            observability::Logger::instance().error("Failed to load certificate from Key Vault", {
                {"secret_name", secret_name},
                {"domain", domain}
            });
            return false;
        }
        
        Certificate cert;
        cert.cert_pem = cert_data->cert_pem;
        cert.key_pem = cert_data->key_pem;
        cert.domain = domain;
        cert.key_vault_uri = key_vault_uri_ + "/secrets/" + secret_name;
        
        // Create SSL context
        auto ctx = create_context(cert, false);
        if (!ctx) {
            return false;
        }
        
        // Store certificate
        std::unique_lock lock(mutex_);
        certificates_[domain] = std::move(cert);
        
        // Update default context if this is the first certificate
        if (certificates_.size() == 1) {
            default_client_context_ = ctx;
        }
        
        observability::Logger::instance().info("Certificate loaded successfully", {
            {"domain", domain},
            {"secret_name", secret_name}
        });
        
        return true;
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Exception loading certificate", {
            {"error", e.what()},
            {"secret_name", secret_name},
            {"domain", domain}
        });
        return false;
    }
}

TLSManager::ssl_context_ptr TLSManager::get_client_context(const std::string& sni_hostname) {
    if (sni_hostname.empty()) {
        return default_client_context_;
    }
    
    // Match SNI hostname to certificate
    std::shared_lock lock(mutex_);
    std::string matched_domain = match_certificate(sni_hostname);
    
    if (!matched_domain.empty() && certificates_.count(matched_domain)) {
        const auto& cert = certificates_.at(matched_domain);
        return create_context(cert, false);
    }
    
    return default_client_context_;
}

TLSManager::ssl_context_ptr TLSManager::get_agent_context() {
    return agent_context_;
}

bool TLSManager::reload_certificate(const std::string& domain) {
    std::shared_lock lock(mutex_);
    
    if (!certificates_.count(domain)) {
        observability::Logger::instance().warning("Cannot reload certificate: domain not found", {
            {"domain", domain}
        });
        return false;
    }
    
    const auto& cert = certificates_.at(domain);
    lock.unlock();
    
    // Extract secret name from Key Vault URI
    std::string secret_name = cert.key_vault_uri;
    size_t pos = secret_name.find("/secrets/");
    if (pos != std::string::npos) {
        secret_name = secret_name.substr(pos + 9);
    }
    
    return load_certificate(secret_name, domain);
}

size_t TLSManager::reload_all_certificates() {
    std::shared_lock lock(mutex_);
    std::vector<std::string> domains;
    domains.reserve(certificates_.size());
    
    for (const auto& [domain, _] : certificates_) {
        domains.push_back(domain);
    }
    lock.unlock();
    
    size_t reloaded = 0;
    for (const auto& domain : domains) {
        if (reload_certificate(domain)) {
            ++reloaded;
        }
    }
    
    observability::Logger::instance().info("Certificate reload completed", {
        {"total", std::to_string(domains.size())},
        {"reloaded", std::to_string(reloaded)}
    });
    
    return reloaded;
}

TLSManager::ssl_context_ptr TLSManager::create_context(
    const Certificate& cert, bool require_client_cert) {
    
    try {
        auto ctx = std::make_shared<ssl_context>(ssl_context::tlsv12_server);
        
        configure_tls_options(*ctx);
        
        // Load certificate chain
        ctx->use_certificate_chain(
            boost::asio::buffer(cert.cert_pem.data(), cert.cert_pem.size())
        );
        
        // Load private key
        ctx->use_private_key(
            boost::asio::buffer(cert.key_pem.data(), cert.key_pem.size()),
            ssl_context::pem
        );
        
        // Require client certificate for mTLS (future use)
        if (require_client_cert) {
            ctx->set_verify_mode(boost::asio::ssl::verify_peer | 
                                boost::asio::ssl::verify_fail_if_no_peer_cert);
        } else {
            ctx->set_verify_mode(boost::asio::ssl::verify_none);
        }
        
        return ctx;
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to create SSL context", {
            {"error", e.what()},
            {"domain", cert.domain}
        });
        return nullptr;
    }
}

void TLSManager::configure_tls_options(ssl_context& ctx) {
    // Enforce TLS 1.2+ minimum version (disable TLS 1.0/1.1 and older)
    ctx.set_options(
        ssl_context::default_workarounds |
        ssl_context::no_sslv2 |
        ssl_context::no_sslv3 |
        ssl_context::no_tlsv1 |
        ssl_context::no_tlsv1_1 |
        ssl_context::single_dh_use
    );
    
    // Set minimum TLS version explicitly (TLS 1.2)
    SSL_CTX_set_min_proto_version(ctx.native_handle(), TLS1_2_VERSION);
    
    // Set cipher suite - use HIGH which includes all strong ciphers available
    // This ensures compatibility across different OpenSSL versions and platforms
    SSL_CTX_set_cipher_list(ctx.native_handle(), "HIGH:!aNULL:!eNULL:!EXPORT:!DES:!MD5:!PSK:!RC4");
    
    observability::Logger::instance().debug("TLS options configured", {
        {"min_version", "TLS 1.2"},
        {"cipher_suites", "ECDHE-ECDSA/RSA-AES-GCM"}
    });
}

std::string TLSManager::match_certificate(const std::string& hostname) const {
    // Exact match first
    if (certificates_.count(hostname)) {
        return hostname;
    }
    
    // Wildcard match (*.domain.com matches api.domain.com)
    for (const auto& [domain, _] : certificates_) {
        if (domain.compare(0, 2, "*.") == 0) {
            std::string suffix = domain.substr(1); // Remove '*'
            size_t suffix_len = suffix.length();
            if (hostname.length() >= suffix_len && 
                hostname.compare(hostname.length() - suffix_len, suffix_len, suffix) == 0) {
                // Verify it's a subdomain match, not partial
                size_t dot_pos = hostname.find('.');
                if (dot_pos != std::string::npos && 
                    hostname.substr(dot_pos) == suffix) {
                    return domain;
                }
            }
        }
    }
    
    return ""; // No match
}

void TLSManager::start_auto_reload(unsigned int reload_interval_minutes) {
    if (!io_context_) {
        observability::Logger::instance().warning("Cannot start auto-reload: no IO context provided");
        return;
    }
    
    if (auto_reload_active_) {
        observability::Logger::instance().warning("Auto-reload already active");
        return;
    }
    
    reload_interval_minutes_ = reload_interval_minutes;
    auto_reload_active_ = true;
    
    // Create timer if not exists
    if (!reload_timer_) {
        reload_timer_ = std::make_unique<boost::asio::steady_timer>(*io_context_);
    }
    
    observability::Logger::instance().info("Certificate auto-reload started", {
        {"interval_minutes", std::to_string(reload_interval_minutes_)}
    });
    
    schedule_reload();
}

void TLSManager::stop_auto_reload() {
    if (!auto_reload_active_) {
        return;
    }
    
    auto_reload_active_ = false;
    
    if (reload_timer_) {
        reload_timer_->cancel();
    }
    
    observability::Logger::instance().info("Certificate auto-reload stopped");
}

void TLSManager::schedule_reload() {
    if (!auto_reload_active_ || !reload_timer_) {
        return;
    }
    
    // Schedule next reload
    reload_timer_->expires_after(std::chrono::minutes(reload_interval_minutes_));
    reload_timer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec) {
            if (ec != boost::asio::error::operation_aborted) {
                observability::Logger::instance().error("Certificate reload timer error", {
                    {"error", ec.message()}
                });
            }
            return;
        }
        
        if (!auto_reload_active_) {
            return;
        }
        
        observability::Logger::instance().info("Starting scheduled certificate reload");
        
        // Reload all certificates
        size_t reloaded = reload_all_certificates();
        
        observability::Logger::instance().info("Scheduled certificate reload completed", {
            {"certificates_reloaded", std::to_string(reloaded)}
        });
        
        // Schedule next reload
        schedule_reload();
    });
}

bool TLSManager::has_certificate_for_domain(const std::string& domain) const {
    std::shared_lock lock(mutex_);
    
    // Check for exact match first
    if (certificates_.find(domain) != certificates_.end()) {
        return true;
    }
    
    // Check for wildcard match
    std::string matched_domain = match_certificate(domain);
    return !matched_domain.empty();
}

}  // namespace security
}  // namespace protogate
