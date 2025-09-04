#include "storage.h"
#include "config.h"
#include "database.h"
#include "crypto.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <algorithm>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace vaultusb {

StorageManager& StorageManager::instance() {
    static StorageManager instance;
    return instance;
}

StorageManager::StorageManager() {
    vault_dir_ = Config::instance().vault_dir();
    ensure_vault_directory();
}

std::string StorageManager::store_file(const std::vector<uint8_t>& file_data, const std::string& original_name, const User& user) {
    if (!CryptoManager::instance().is_unlocked()) {
        throw std::runtime_error("Vault is locked");
    }
    
    try {
        std::string file_id = generate_file_id();
        std::string encrypted_name = generate_encrypted_filename();
        std::string encrypted_path = get_encrypted_file_path(encrypted_name);
        
        // Write file to disk
        std::ofstream file(encrypted_path, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        
        file.write(reinterpret_cast<const char*>(file_data.data()), file_data.size());
        file.close();
        
        // Encrypt the file
        if (!CryptoManager::instance().encrypt_file(encrypted_path, file_id)) {
            unlink(encrypted_path.c_str());
            return "";
        }
        
        // Store file metadata in database
        File file_record(file_id, original_name, encrypted_name, file_data.size(), get_mime_type(original_name), user.id);
        if (!Database::instance().create_file(file_record)) {
            unlink(encrypted_path.c_str());
            return "";
        }
        
        return file_id;
    } catch (const std::exception& e) {
        std::cerr << "Failed to store file: " << e.what() << std::endl;
        return "";
    }
}

std::vector<uint8_t> StorageManager::retrieve_file(const std::string& file_id, const User& user) {
    if (!CryptoManager::instance().is_unlocked()) {
        throw std::runtime_error("Vault is locked");
    }
    
    try {
        auto file_record = Database::instance().get_file_by_id(file_id);
        if (!file_record || file_record->user_id != user.id) {
            return {};
        }
        
        std::string encrypted_path = get_encrypted_file_path(file_record->encrypted_name);
        if (!file_exists(encrypted_path)) {
            return {};
        }
        
        return CryptoManager::instance().decrypt_file(encrypted_path, file_id);
    } catch (const std::exception& e) {
        std::cerr << "Failed to retrieve file: " << e.what() << std::endl;
        return {};
    }
}

bool StorageManager::delete_file(const std::string& file_id, const User& user) {
    if (!CryptoManager::instance().is_unlocked()) {
        throw std::runtime_error("Vault is locked");
    }
    
    try {
        auto file_record = Database::instance().get_file_by_id(file_id);
        if (!file_record || file_record->user_id != user.id) {
            return false;
        }
        
        // Mark as deleted in database
        if (!Database::instance().delete_file(file_id)) {
            return false;
        }
        
        // Securely delete the encrypted file
        std::string encrypted_path = get_encrypted_file_path(file_record->encrypted_name);
        if (file_exists(encrypted_path)) {
            CryptoManager::instance().secure_delete(encrypted_path);
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to delete file: " << e.what() << std::endl;
        return false;
    }
}

std::vector<File> StorageManager::list_files(const User& user, int limit, int offset) {
    return Database::instance().get_user_files(user.id, limit, offset);
}

std::shared_ptr<File> StorageManager::get_file_info(const std::string& file_id, const User& user) {
    auto file = Database::instance().get_file_by_id(file_id);
    if (file && file->user_id == user.id) {
        return file;
    }
    return nullptr;
}

std::vector<File> StorageManager::search_files(const std::string& query, const User& user, int limit) {
    // Simple implementation - in production, use full-text search
    auto files = list_files(user, 1000, 0); // Get all files
    std::vector<File> results;
    
    for (const auto& file : files) {
        if (file.original_name.find(query) != std::string::npos) {
            results.push_back(file);
            if (results.size() >= static_cast<size_t>(limit)) {
                break;
            }
        }
    }
    
    return results;
}

StorageManager::StorageStats StorageManager::get_storage_stats(const User& user) {
    auto files = list_files(user, 10000, 0); // Get all files
    
    StorageStats stats;
    stats.file_count = files.size();
    
    for (const auto& file : files) {
        stats.total_size += file.size;
    }
    
    stats.total_size_mb = static_cast<double>(stats.total_size) / (1024.0 * 1024.0);
    
    return stats;
}

void StorageManager::cleanup_deleted_files() {
    // This would be implemented to clean up files marked as deleted
    // For now, it's a placeholder
}

std::string StorageManager::generate_file_id() {
    std::ostringstream oss;
    oss << std::hex << std::time(nullptr) << std::rand();
    return oss.str();
}

std::string StorageManager::generate_encrypted_filename() {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::string result;
    
    for (int i = 0; i < 32; i++) {
        result += chars[std::rand() % chars.length()];
    }
    
    return result;
}

std::string StorageManager::get_mime_type(const std::string& filename) {
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return "application/octet-stream";
    }
    
    std::string extension = filename.substr(dot_pos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    // Simple MIME type mapping
    if (extension == "txt") return "text/plain";
    if (extension == "html" || extension == "htm") return "text/html";
    if (extension == "css") return "text/css";
    if (extension == "js") return "application/javascript";
    if (extension == "json") return "application/json";
    if (extension == "xml") return "application/xml";
    if (extension == "pdf") return "application/pdf";
    if (extension == "zip") return "application/zip";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "png") return "image/png";
    if (extension == "gif") return "image/gif";
    if (extension == "svg") return "image/svg+xml";
    if (extension == "mp4") return "video/mp4";
    if (extension == "mp3") return "audio/mpeg";
    
    return "application/octet-stream";
}

bool StorageManager::ensure_vault_directory() {
    return create_directory(vault_dir_);
}

std::string StorageManager::get_encrypted_file_path(const std::string& encrypted_name) {
    return vault_dir_ + "/" + encrypted_name;
}

bool StorageManager::file_exists(const std::string& file_path) {
    struct stat buffer;
    return stat(file_path.c_str(), &buffer) == 0;
}

bool StorageManager::create_directory(const std::string& path) {
    return system(("mkdir -p " + path).c_str()) == 0;
}

} // namespace vaultusb
