#pragma once

#include <string>
#include <optional>
#include <memory>

namespace protogate {
namespace storage {

/**
 * @brief Azure Key Vault client for secrets and certificates
 * 
 * Responsibilities:
 * - Retrieve secrets (tokens) from Azure Key Vault
 * - Retrieve certificates (TLS certs) from Azure Key Vault
 * - Store secrets in Key Vault
 * - Support Azure SDK authentication (Managed Identity, DefaultAzureCredential)
 * 
 * Note: This is a wrapper around Azure SDK for C++ (azure-security-keyvault-secrets)
 */
class KeyVaultClient {
public:
    /**
     * @brief Certificate data structure
     */
    struct CertificateData {
        std::string cert_pem;  // PEM-encoded certificate chain
        std::string key_pem;   // PEM-encoded private key
        std::string version;   // Certificate version from Key Vault
    };

    /**
     * @brief Initialize Key Vault client
     * @param vault_uri Full URI of Key Vault (https://<vault-name>.vault.azure.net)
     */
    explicit KeyVaultClient(const std::string& vault_uri);

    /**
     * @brief Get secret value from Key Vault
     * @param secret_name Name of the secret
     * @param version Optional version (empty = latest)
     * @return Secret value or nullopt if not found
     */
    std::optional<std::string> get_secret(const std::string& secret_name, 
                                         const std::string& version = "");

    /**
     * @brief Set secret value in Key Vault
     * @param secret_name Name of the secret
     * @param secret_value Value to store
     * @return true if successful
     */
    bool set_secret(const std::string& secret_name, const std::string& secret_value);

    /**
     * @brief Delete secret from Key Vault
     * @param secret_name Name of the secret
     * @return true if successful
     */
    bool delete_secret(const std::string& secret_name);

    /**
     * @brief Get certificate with private key from Key Vault
     * @param cert_name Name of the certificate
     * @param version Optional version (empty = latest)
     * @return Certificate data or nullopt if not found
     */
    std::optional<CertificateData> get_certificate(const std::string& cert_name,
                                                   const std::string& version = "");

    /**
     * @brief Import certificate to Key Vault
     * @param cert_name Name for the certificate
     * @param cert_pem PEM-encoded certificate chain
     * @param key_pem PEM-encoded private key
     * @return true if successful
     */
    bool import_certificate(const std::string& cert_name, 
                           const std::string& cert_pem,
                           const std::string& key_pem);

private:
    /**
     * @brief Get Azure credential for authentication
     * Uses DefaultAzureCredential (Managed Identity in production, env vars for dev)
     */
    std::shared_ptr<void> get_credential();

    /**
     * @brief Parse PFX/PKCS12 format to PEM
     */
    std::optional<CertificateData> parse_pfx(const std::string& pfx_data);

    std::string vault_uri_;
    std::shared_ptr<void> credential_;
};

}  // namespace storage
}  // namespace protogate
