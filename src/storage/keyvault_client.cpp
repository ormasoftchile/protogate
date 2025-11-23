#include "keyvault_client.h"
#include "../observability/logger.h"
#include <stdexcept>
#include <openssl/pkcs12.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <vector>

// Note: Azure SDK for C++ integration will be added in production
// For MVP, this is a stub implementation that simulates Key Vault operations
// TODO: Add azure-security-keyvault-secrets dependency and implement real client

namespace protogate {
namespace storage {

KeyVaultClient::KeyVaultClient(const std::string& vault_uri)
    : vault_uri_(vault_uri) {
    
    observability::Logger::instance().info("KeyVaultClient initialized", {
        {"vault_uri", vault_uri_}
    });
    
    // TODO: Initialize Azure SDK credential
    // credential_ = std::make_shared<Azure::Identity::DefaultAzureCredential>();
}

std::optional<std::string> KeyVaultClient::get_secret(
    const std::string& secret_name, const std::string& version) {
    
    try {
        observability::Logger::instance().debug("Getting secret from Key Vault", {
            {"secret_name", secret_name},
            {"version", version.empty() ? "latest" : version}
        });
        
        // TODO: Implement Azure SDK call
        // Azure::Security::KeyVault::Secrets::SecretClient client(vault_uri_, credential_);
        // auto response = client.GetSecret(secret_name);
        // return response.Value.Value;
        
        // Stub: Return dummy value for development
        observability::Logger::instance().warning("KeyVault stub: returning dummy secret");
        return "dummy_secret_value";
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to get secret", {
            {"error", e.what()},
            {"secret_name", secret_name}
        });
        return std::nullopt;
    }
}

bool KeyVaultClient::set_secret(const std::string& secret_name, const std::string& secret_value) {
    try {
        observability::Logger::instance().debug("Setting secret in Key Vault", {
            {"secret_name", secret_name}
        });
        
        // TODO: Implement Azure SDK call
        // Azure::Security::KeyVault::Secrets::SecretClient client(vault_uri_, credential_);
        // client.SetSecret(secret_name, secret_value);
        
        observability::Logger::instance().info("Secret set successfully (stub)", {
            {"secret_name", secret_name}
        });
        
        return true;
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to set secret", {
            {"error", e.what()},
            {"secret_name", secret_name}
        });
        return false;
    }
}

bool KeyVaultClient::delete_secret(const std::string& secret_name) {
    try {
        observability::Logger::instance().debug("Deleting secret from Key Vault", {
            {"secret_name", secret_name}
        });
        
        // TODO: Implement Azure SDK call
        // Azure::Security::KeyVault::Secrets::SecretClient client(vault_uri_, credential_);
        // client.StartDeleteSecret(secret_name);
        
        observability::Logger::instance().info("Secret deleted successfully (stub)", {
            {"secret_name", secret_name}
        });
        
        return true;
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to delete secret", {
            {"error", e.what()},
            {"secret_name", secret_name}
        });
        return false;
    }
}

std::optional<KeyVaultClient::CertificateData> KeyVaultClient::get_certificate(
    const std::string& cert_name, const std::string& version) {
    
    try {
        observability::Logger::instance().debug("Getting certificate from Key Vault", {
            {"cert_name", cert_name},
            {"version", version.empty() ? "latest" : version}
        });
        
        // TODO: Implement Azure SDK call
        // Azure::Security::KeyVault::Certificates::CertificateClient client(vault_uri_, credential_);
        // auto response = client.GetCertificate(cert_name);
        // 
        // Detect format and parse accordingly:
        // - If binary data (PFX/PKCS#12): return parse_pfx(response.Value.Cer);
        // - If text data (PEM): return parse_pem(response.Value.Cer);
        //
        // For secret-based storage (cert+key as secret):
        // Azure::Security::KeyVault::Secrets::SecretClient secret_client(vault_uri_, credential_);
        // auto secret_response = secret_client.GetSecret(cert_name);
        // return parse_pem(secret_response.Value.Value);
        
        // Stub: Return dummy certificate for development
        // Simulate PEM format (most common for Azure Key Vault secrets)
        std::string dummy_pem = 
            "-----BEGIN CERTIFICATE-----\n"
            "MIIDazCCAlOgAwIBAgIUXxQvvQZ1234567890abcdefghijklmnoEwDQYJKoZIhvcNAQEL\n"
            "BQAwRTELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWExITAfBgNVBAoMGElu\n"
            "dGVybmV0IFdpZGdpdHMgUHR5IEx0ZDAeFw0yNTAxMDEwMDAwMDBaFw0yNjAxMDEwMDAw\n"
            "MDBaMEUxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMSEwHwYDVQQKDBhJ\n"
            "bnRlcm5ldCBXaWRnaXRzIFB0eSBMdGQwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK\n"
            "AoIBAQDummy_cert_data_here_not_real_certificate_1234567890abcdefghij\n"
            "-----END CERTIFICATE-----\n"
            "-----BEGIN PRIVATE KEY-----\n"
            "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDdummy_key_data\n"
            "here_not_real_private_key_1234567890abcdefghijklmnopqrstuvwxyz0123456789\n"
            "-----END PRIVATE KEY-----\n";
        
        auto result = parse_pem(dummy_pem);
        if (!result) {
            observability::Logger::instance().error("Failed to parse dummy certificate PEM");
            return std::nullopt;
        }
        
        observability::Logger::instance().warning("KeyVault stub: returning dummy certificate");
        return result;
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to get certificate", {
            {"error", e.what()},
            {"cert_name", cert_name}
        });
        return std::nullopt;
    }
}

bool KeyVaultClient::import_certificate(
    const std::string& cert_name, 
    const std::string& cert_pem,
    const std::string& key_pem) {
    
    try {
        observability::Logger::instance().debug("Importing certificate to Key Vault", {
            {"cert_name", cert_name}
        });
        
        // TODO: Implement Azure SDK call
        // Convert PEM to PFX, then upload
        
        observability::Logger::instance().info("Certificate imported successfully (stub)", {
            {"cert_name", cert_name}
        });
        
        return true;
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to import certificate", {
            {"error", e.what()},
            {"cert_name", cert_name}
        });
        return false;
    }
}

std::shared_ptr<void> KeyVaultClient::get_credential() {
    // TODO: Return Azure::Identity::DefaultAzureCredential
    return nullptr;
}

std::optional<KeyVaultClient::CertificateData> KeyVaultClient::parse_pfx(const std::string& pfx_data) {
    try {
        // Create BIO from PFX data
        BIO* bio = BIO_new_mem_buf(pfx_data.data(), static_cast<int>(pfx_data.size()));
        if (!bio) {
            observability::Logger::instance().error("Failed to create BIO for PFX data");
            return std::nullopt;
        }

        // Parse PKCS#12 structure (PFX format)
        PKCS12* p12 = d2i_PKCS12_bio(bio, nullptr);
        BIO_free(bio);
        
        if (!p12) {
            observability::Logger::instance().error("Failed to parse PKCS#12 data");
            ERR_clear_error();
            return std::nullopt;
        }

        // Extract certificate and private key
        EVP_PKEY* pkey = nullptr;
        X509* cert = nullptr;
        STACK_OF(X509)* ca_stack = nullptr;
        
        // Try with empty password first, then common passwords
        const char* passwords[] = {"", nullptr};
        bool parsed = false;
        
        for (const char* password : passwords) {
            if (PKCS12_parse(p12, password, &pkey, &cert, &ca_stack)) {
                parsed = true;
                break;
            }
            ERR_clear_error();
        }
        
        PKCS12_free(p12);
        
        if (!parsed || !pkey || !cert) {
            observability::Logger::instance().error("Failed to parse PKCS#12 contents");
            if (pkey) EVP_PKEY_free(pkey);
            if (cert) X509_free(cert);
            if (ca_stack) sk_X509_pop_free(ca_stack, X509_free);
            return std::nullopt;
        }

        CertificateData result;
        
        // Convert certificate to PEM
        BIO* cert_bio = BIO_new(BIO_s_mem());
        if (PEM_write_bio_X509(cert_bio, cert) != 1) {
            observability::Logger::instance().error("Failed to write certificate to PEM");
            EVP_PKEY_free(pkey);
            X509_free(cert);
            if (ca_stack) sk_X509_pop_free(ca_stack, X509_free);
            BIO_free(cert_bio);
            return std::nullopt;
        }
        
        // Write CA certificates if present
        if (ca_stack) {
            for (int i = 0; i < sk_X509_num(ca_stack); i++) {
                X509* ca_cert = sk_X509_value(ca_stack, i);
                PEM_write_bio_X509(cert_bio, ca_cert);
            }
        }
        
        // Read certificate chain PEM data
        char* cert_data = nullptr;
        long cert_len = BIO_get_mem_data(cert_bio, &cert_data);
        result.cert_pem = std::string(cert_data, cert_len);
        BIO_free(cert_bio);
        
        // Convert private key to PEM
        BIO* key_bio = BIO_new(BIO_s_mem());
        if (PEM_write_bio_PrivateKey(key_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
            observability::Logger::instance().error("Failed to write private key to PEM");
            EVP_PKEY_free(pkey);
            X509_free(cert);
            if (ca_stack) sk_X509_pop_free(ca_stack, X509_free);
            BIO_free(key_bio);
            return std::nullopt;
        }
        
        // Read private key PEM data
        char* key_data = nullptr;
        long key_len = BIO_get_mem_data(key_bio, &key_data);
        result.key_pem = std::string(key_data, key_len);
        BIO_free(key_bio);
        
        // Cleanup
        EVP_PKEY_free(pkey);
        X509_free(cert);
        if (ca_stack) sk_X509_pop_free(ca_stack, X509_free);
        
        result.version = "1";
        
        observability::Logger::instance().info("Successfully parsed PFX certificate");
        return result;
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Exception parsing PFX", {
            {"error", e.what()}
        });
        ERR_clear_error();
        return std::nullopt;
    }
}

std::optional<KeyVaultClient::CertificateData> KeyVaultClient::parse_pem(const std::string& pem_data) {
    try {
        CertificateData result;
        
        // Parse PEM data which may contain certificate and/or key
        BIO* bio = BIO_new_mem_buf(pem_data.data(), static_cast<int>(pem_data.size()));
        if (!bio) {
            observability::Logger::instance().error("Failed to create BIO for PEM data");
            return std::nullopt;
        }

        // Extract certificate(s)
        std::string cert_pem;
        X509* cert = nullptr;
        while ((cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) != nullptr) {
            BIO* cert_bio = BIO_new(BIO_s_mem());
            PEM_write_bio_X509(cert_bio, cert);
            
            char* cert_data = nullptr;
            long cert_len = BIO_get_mem_data(cert_bio, &cert_data);
            cert_pem.append(cert_data, cert_len);
            
            BIO_free(cert_bio);
            X509_free(cert);
        }
        ERR_clear_error();
        
        // Reset BIO to beginning for private key extraction
        BIO_free(bio);
        bio = BIO_new_mem_buf(pem_data.data(), static_cast<int>(pem_data.size()));
        
        // Extract private key
        std::string key_pem;
        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
        if (pkey) {
            BIO* key_bio = BIO_new(BIO_s_mem());
            PEM_write_bio_PrivateKey(key_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
            
            char* key_data = nullptr;
            long key_len = BIO_get_mem_data(key_bio, &key_data);
            key_pem = std::string(key_data, key_len);
            
            BIO_free(key_bio);
            EVP_PKEY_free(pkey);
        }
        ERR_clear_error();
        
        BIO_free(bio);
        
        if (cert_pem.empty()) {
            observability::Logger::instance().error("No certificate found in PEM data");
            return std::nullopt;
        }
        
        result.cert_pem = cert_pem;
        result.key_pem = key_pem;
        result.version = "1";
        
        observability::Logger::instance().info("Successfully parsed PEM certificate", {
            {"has_private_key", key_pem.empty() ? "false" : "true"}
        });
        
        return result;
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Exception parsing PEM", {
            {"error", e.what()}
        });
        ERR_clear_error();
        return std::nullopt;
    }
}

bool KeyVaultClient::validate_cert_key_pair(const std::string& cert_pem, const std::string& key_pem) {
    try {
        // Load certificate
        BIO* cert_bio = BIO_new_mem_buf(cert_pem.data(), static_cast<int>(cert_pem.size()));
        X509* cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
        BIO_free(cert_bio);
        
        if (!cert) {
            observability::Logger::instance().error("Failed to load certificate for validation");
            ERR_clear_error();
            return false;
        }
        
        // Load private key
        BIO* key_bio = BIO_new_mem_buf(key_pem.data(), static_cast<int>(key_pem.size()));
        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
        BIO_free(key_bio);
        
        if (!pkey) {
            observability::Logger::instance().error("Failed to load private key for validation");
            X509_free(cert);
            ERR_clear_error();
            return false;
        }
        
        // Verify certificate and key match
        EVP_PKEY* cert_pubkey = X509_get_pubkey(cert);
        
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        // OpenSSL 3.0+
        bool match = (EVP_PKEY_eq(cert_pubkey, pkey) == 1);
#else
        // OpenSSL 1.1.x
        bool match = (EVP_PKEY_cmp(cert_pubkey, pkey) == 1);
#endif
        
        EVP_PKEY_free(cert_pubkey);
        EVP_PKEY_free(pkey);
        X509_free(cert);
        ERR_clear_error();
        
        if (!match) {
            observability::Logger::instance().error("Certificate and private key do not match");
            return false;
        }
        
        observability::Logger::instance().info("Certificate and private key validated successfully");
        return true;
        
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Exception validating cert/key pair", {
            {"error", e.what()}
        });
        ERR_clear_error();
        return false;
    }
}

}  // namespace storage
}  // namespace protogate
