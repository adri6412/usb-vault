#pragma once

#include "models.h"
#include "database.h"
#include "crypto.h"
#include <string>
#include <vector>
#include <memory>

namespace vaultusb {

class StorageManager {
public:
    static StorageManager& instance();
    
    // File operations
    std::string store_file(const std::vector<uint8_t>& file_data, const std::string& original_name, const User& user);
    std::vector<uint8_t> retrieve_file(const std::string& file_id, const User& user);
    bool delete_file(const std::string& file_id, const User& user);
    std::vector<File> list_files(const User& user, int limit = 100, int offset = 0);
    std::shared_ptr<File> get_file_info(const std::string& file_id, const User& user);
    std::vector<File> search_files(const std::string& query, const User& user, int limit = 100);
    
    // Storage statistics
    struct StorageStats {
        int total_size = 0;
        int file_count = 0;
        double total_size_mb = 0.0;
    };
    StorageStats get_storage_stats(const User& user);
    
    // Maintenance
    void cleanup_deleted_files();
    
private:
    StorageManager() = default;
    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;
    
    std::string vault_dir_;
    
    // Helper methods
    std::string generate_file_id();
    std::string generate_encrypted_filename();
    std::string get_mime_type(const std::string& filename);
    bool ensure_vault_directory();
    
    // File path operations
    std::string get_encrypted_file_path(const std::string& encrypted_name);
    bool file_exists(const std::string& file_path);
    bool create_directory(const std::string& path);
};

} // namespace vaultusb
