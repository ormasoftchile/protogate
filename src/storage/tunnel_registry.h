#pragma once

#include "../models/tunnel.h"
#include "cache.h"
#include <memory>
#include <chrono>
#include <functional>
#include <thread>
#include <atomic>

namespace protogate {
namespace storage {

/**
 * @brief Thread-safe tunnel registry with periodic persistence to Azure Blob Storage
 * 
 * Maintains in-memory cache of tunnel configurations with automatic backup to blob storage.
 * Loads tunnels from blob storage on startup and saves every 5 minutes.
 */
class TunnelRegistry {
public:
    /**
     * @brief Create registry with optional blob storage backup
     * @param storage_account_name Azure Storage account name (empty = no persistence)
     * @param container_name Blob container name (default: "protogate-tunnels")
     * @param backup_interval_seconds Seconds between backups (default: 300 = 5 minutes)
     */
    explicit TunnelRegistry(
        const std::string& storage_account_name = "",
        const std::string& container_name = "protogate-tunnels",
        int backup_interval_seconds = 300
    );
    
    ~TunnelRegistry();
    
    /**
     * @brief Load tunnels from blob storage (called on startup)
     * @return Number of tunnels loaded
     */
    size_t load_from_storage();
    
    /**
     * @brief Save tunnels to blob storage (called periodically)
     * @return true if save succeeded
     */
    bool save_to_storage();
    
    /**
     * @brief Start automatic backup timer
     */
    void start_backup_timer();
    
    /**
     * @brief Stop automatic backup timer
     */
    void stop_backup_timer();
    
    /**
     * @brief Register a tunnel in the registry
     */
    void register_tunnel(const models::Tunnel& tunnel);
    
    /**
     * @brief Get tunnel by ID
     * @return std::nullopt if tunnel not found
     */
    std::optional<models::Tunnel> get_tunnel(const std::string& tunnel_id) const;
    
    /**
     * @brief Update existing tunnel configuration
     * @return true if tunnel was found and updated
     */
    bool update_tunnel(const models::Tunnel& tunnel);
    
    /**
     * @brief Remove tunnel from registry
     * @return true if tunnel was found and removed
     */
    bool remove_tunnel(const std::string& tunnel_id);
    
    /**
     * @brief Check if tunnel exists
     */
    bool has_tunnel(const std::string& tunnel_id) const;
    
    /**
     * @brief Get all registered tunnels
     */
    std::vector<models::Tunnel> get_all_tunnels() const;
    
    /**
     * @brief Get number of registered tunnels
     */
    size_t size() const;
    
    /**
     * @brief Clear all tunnels (for testing)
     */
    void clear();
    
    /**
     * @brief Set callback for backup completion (for testing/monitoring)
     */
    void set_backup_callback(std::function<void(bool success, size_t tunnel_count)> callback);

private:
    // In-memory cache of tunnels (no TTL expiration for tunnel configs)
    Cache<std::string, models::Tunnel> cache_;
    
    // Azure Blob Storage configuration
    std::string storage_account_name_;
    std::string container_name_;
    std::string blob_name_;
    bool persistence_enabled_;
    
    // Backup timer
    int backup_interval_seconds_;
    bool backup_timer_running_;
    std::unique_ptr<std::thread> backup_thread_;
    std::atomic<bool> stop_backup_flag_;
    
    // Callback for monitoring
    std::function<void(bool success, size_t tunnel_count)> backup_callback_;
    
    // Backup implementation
    void backup_loop();
    std::string serialize_tunnels() const;
    bool deserialize_tunnels(const std::string& json);
    
    // Azure Blob Storage operations (stubbed for now - will use Azure SDK)
    bool upload_to_blob(const std::string& data);
    std::optional<std::string> download_from_blob();
};

}  // namespace storage
}  // namespace protogate
