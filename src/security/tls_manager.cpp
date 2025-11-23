#include "tls_manager.h"
#include "../observability/logger.h"
#include "../storage/keyvault_client.h"
#include <boost/asio/ssl/context.hpp>
#include <shared_mutex>
#include <algorithm>

namespace protogate {
namespace security {

TLSManager::TLSManager(const std::string& key_vault_uri)
    : key_vault_uri_(key_vault_uri) {
    
    // Create default contexts
    default_client_context_ = std::make_shared<ssl_context>(ssl_context::tlsv12_server);
    agent_context_ = std::make_shared<ssl_context>(ssl_context::tlsv12_server);
    
    configure_tls_options(*default_client_context_);
    configure_tls_options(*agent_context_);
    
    observability::Logger::instance().info("TLSManager initialized", {
        {"key_vault_uri", key_vault_uri_}
    });
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
    
    // Set cipher suite (strong ciphers only - ECDHE for forward secrecy, AES-GCM for AEAD)
    SSL_CTX_set_cipher_list(ctx.native_handle(), 
        "ECDHE-ECDSA-AES256-GCM-SHA384:"
        "ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-ECDSA-AES128-GCM-SHA256:"
        "ECDHE-RSA-AES128-GCM-SHA256"
    );
    
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

}  // namespace security
}  // namespace protogate
