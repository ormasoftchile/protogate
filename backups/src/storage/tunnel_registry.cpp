#include "tunnel_registry.h"
#include "../observability/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>

using json = nlohmann::json;

namespace protogate {
namespace storage {

TunnelRegistry::TunnelRegistry(
    const std::string& storage_account_name,
    const std::string& container_name,
    int backup_interval_seconds
)
    : cache_(std::chrono::seconds(0))  // No TTL for tunnel configs
    , storage_account_name_(storage_account_name)
    , container_name_(container_name)
    , blob_name_("tunnels.json")
    , persistence_enabled_(!storage_account_name.empty())
    , backup_interval_seconds_(backup_interval_seconds)
    , backup_timer_running_(false)
    , stop_backup_flag_(false)
{
    if (persistence_enabled_) {
        observability::Logger::instance().info("TunnelRegistry initialized with persistence",
            {{"storage_account", storage_account_name_},
             {"container", container_name_},
             {"backup_interval", std::to_string(backup_interval_seconds_)}});
    } else {
        observability::Logger::instance().info("TunnelRegistry initialized without persistence (in-memory only)");
    }
}

TunnelRegistry::~TunnelRegistry() {
    stop_backup_timer();
}

size_t TunnelRegistry::load_from_storage() {
    if (!persistence_enabled_) {
        observability::Logger::instance().debug("Persistence disabled, skipping load");
        return 0;
    }
    
    try {
        auto json_data = download_from_blob();
        if (!json_data) {
            observability::Logger::instance().info("No existing tunnel data found in blob storage");
            return 0;
        }
        
        if (deserialize_tunnels(*json_data)) {
            size_t count = size();
            observability::Logger::instance().info("Loaded tunnels from blob storage", {{"count", std::to_string(count)}});
            return count;
        } else {
            observability::Logger::instance().error("Failed to deserialize tunnel data from blob storage");
            return 0;
        }
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Error loading tunnels from storage",
            {{"error", e.what()}});
        return 0;
    }
}

bool TunnelRegistry::save_to_storage() {
    if (!persistence_enabled_) {
        return true;  // No-op if persistence disabled
    }
    
    try {
        std::string json_data = serialize_tunnels();
        bool success = upload_to_blob(json_data);
        
        if (success) {
            observability::Logger::instance().debug("Saved tunnels to blob storage", {{"count", std::to_string(size())}});
            if (backup_callback_) {
                backup_callback_(true, size());
            }
        } else {
            observability::Logger::instance().error("Failed to upload tunnels to blob storage");
            if (backup_callback_) {
                backup_callback_(false, size());
            }
        }
        
        return success;
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Error saving tunnels to storage",
            {{"error", e.what()}});
        if (backup_callback_) {
            backup_callback_(false, size());
        }
        return false;
    }
}

void TunnelRegistry::start_backup_timer() {
    if (!persistence_enabled_) {
        return;
    }
    
    if (backup_timer_running_) {
        observability::Logger::instance().warning("Backup timer already running");
        return;
    }
    
    stop_backup_flag_ = false;
    backup_timer_running_ = true;
    
    backup_thread_ = std::make_unique<std::thread>([this]() {
        backup_loop();
    });
    
    observability::Logger::instance().info("Started backup timer",
        {{"interval_seconds", std::to_string(backup_interval_seconds_)}});
}

void TunnelRegistry::stop_backup_timer() {
    if (!backup_timer_running_) {
        return;
    }
    
    stop_backup_flag_ = true;
    
    if (backup_thread_ && backup_thread_->joinable()) {
        backup_thread_->join();
    }
    
    backup_timer_running_ = false;
    observability::Logger::instance().info("Stopped backup timer");
}

void TunnelRegistry::backup_loop() {
    while (!stop_backup_flag_) {
        // Sleep in small increments to allow quick shutdown
        for (int i = 0; i < backup_interval_seconds_ && !stop_backup_flag_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (!stop_backup_flag_) {
            save_to_storage();
        }
    }
}

void TunnelRegistry::register_tunnel(const models::Tunnel& tunnel) {
    tunnel.validate();  // Throws if invalid
    cache_.put(tunnel.tunnel_id, tunnel);
    observability::Logger::instance().info("Registered tunnel",
        {{"tunnel_id", tunnel.tunnel_id},
         {"protocol", tunnel.protocol == models::TunnelProtocol::HTTP ? "HTTP" : "TCP"}});
}

std::optional<models::Tunnel> TunnelRegistry::get_tunnel(const std::string& tunnel_id) const {
    return cache_.get(tunnel_id);
}

bool TunnelRegistry::update_tunnel(const models::Tunnel& tunnel) {
    tunnel.validate();  // Throws if invalid
    
    if (!cache_.contains(tunnel.tunnel_id)) {
        return false;
    }
    
    cache_.put(tunnel.tunnel_id, tunnel);
    observability::Logger::instance().info("Updated tunnel", {{"tunnel_id", tunnel.tunnel_id}});
    return true;
}

bool TunnelRegistry::remove_tunnel(const std::string& tunnel_id) {
    bool removed = cache_.remove(tunnel_id);
    if (removed) {
        observability::Logger::instance().info("Removed tunnel", {{"tunnel_id", tunnel_id}});
    }
    return removed;
}

bool TunnelRegistry::has_tunnel(const std::string& tunnel_id) const {
    return cache_.contains(tunnel_id);
}

std::vector<models::Tunnel> TunnelRegistry::get_all_tunnels() const {
    std::vector<models::Tunnel> tunnels;
    cache_.for_each([&tunnels](const std::string& key, const models::Tunnel& tunnel) {
        tunnels.push_back(tunnel);
    });
    return tunnels;
}

size_t TunnelRegistry::size() const {
    return cache_.size();
}

void TunnelRegistry::clear() {
    cache_.clear();
    observability::Logger::instance().info("Cleared all tunnels from registry");
}

void TunnelRegistry::set_backup_callback(std::function<void(bool, size_t)> callback) {
    backup_callback_ = std::move(callback);
}

std::string TunnelRegistry::serialize_tunnels() const {
    json j = json::array();
    
    cache_.for_each([&j](const std::string& key, const models::Tunnel& tunnel) {
        j.push_back(json::parse(tunnel.to_json()));
    });
    
    return j.dump(2);  // Pretty-print with 2-space indent
}

bool TunnelRegistry::deserialize_tunnels(const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        
        if (!j.is_array()) {
            observability::Logger::instance().error("Invalid tunnel data format: expected array");
            return false;
        }
        
        for (const auto& tunnel_json : j) {
            try {
                models::Tunnel tunnel = models::Tunnel::from_json(tunnel_json.dump());
                cache_.put(tunnel.tunnel_id, tunnel);
            } catch (const std::exception& e) {
                observability::Logger::instance().error("Failed to deserialize tunnel",
                    {{"error", e.what()},
                     {"tunnel_data", tunnel_json.dump()}});
                // Continue with other tunnels
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to parse tunnel JSON",
            {{"error", e.what()}});
        return false;
    }
}

bool TunnelRegistry::upload_to_blob(const std::string& data) {
    // TODO: Implement Azure Blob Storage upload using Azure SDK for C++
    // For now, write to local file as fallback (useful for testing)
    
    std::string fallback_path = "/tmp/protogate-tunnels-backup.json";
    
    try {
        std::ofstream file(fallback_path);
        if (!file.is_open()) {
            observability::Logger::instance().error("Failed to open fallback backup file",
                {{"path", fallback_path}});
            return false;
        }
        
        file << data;
        file.close();
        
        observability::Logger::instance().debug("Wrote tunnel backup to local file (fallback)",
            {{"path", fallback_path},
             {"size_bytes", std::to_string(data.size())}});
        
        return true;
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to write fallback backup file",
            {{"path", fallback_path},
             {"error", e.what()}});
        return false;
    }
    
    // Future implementation with Azure SDK:
    // auto credential = std::make_shared<Azure::Identity::DefaultAzureCredential>();
    // std::string blob_url = "https://" + storage_account_name_ + ".blob.core.windows.net";
    // auto blob_client = Azure::Storage::Blobs::BlobContainerClient(blob_url, container_name_, credential);
    // auto block_blob = blob_client.GetBlockBlobClient(blob_name_);
    // block_blob.UploadFrom(data);
}

std::optional<std::string> TunnelRegistry::download_from_blob() {
    // TODO: Implement Azure Blob Storage download using Azure SDK for C++
    // For now, read from local file as fallback
    
    std::string fallback_path = "/tmp/protogate-tunnels-backup.json";
    
    try {
        std::ifstream file(fallback_path);
        if (!file.is_open()) {
            // File doesn't exist - this is OK on first run
            return std::nullopt;
        }
        
        std::string data((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        file.close();
        
        observability::Logger::instance().debug("Loaded tunnel backup from local file (fallback)",
            {{"path", fallback_path},
             {"size_bytes", std::to_string(data.size())}});
        
        return data;
    } catch (const std::exception& e) {
        observability::Logger::instance().error("Failed to read fallback backup file",
            {{"path", fallback_path},
             {"error", e.what()}});
        return std::nullopt;
    }
    
    // Future implementation with Azure SDK:
    // auto credential = std::make_shared<Azure::Identity::DefaultAzureCredential>();
    // std::string blob_url = "https://" + storage_account_name_ + ".blob.core.windows.net";
    // auto blob_client = Azure::Storage::Blobs::BlobContainerClient(blob_url, container_name_, credential);
    // auto block_blob = blob_client.GetBlockBlobClient(blob_name_);
    // auto download_result = block_blob.Download();
    // return download_result.Value.BodyStream->ReadToEnd();
}

}  // namespace storage
}  // namespace protogate
