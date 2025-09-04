#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace vaultusb {

class CryptoManager {
public:
    static CryptoManager& instance();
    
    // Master key management
    std::vector<uint8_t> generate_master_key();
    std::string seal_master_key(const std::vector<uint8_t>& master_key, const std::string& password);
    std::vector<uint8_t> unseal_master_key(const std::string& sealed_data, const std::string& password);
    bool load_master_key(const std::string& password);
    bool save_master_key(const std::vector<uint8_t>& master_key, const std::string& password);
    
    // File encryption/decryption
    std::vector<uint8_t> derive_file_key(const std::string& file_id);
    bool encrypt_file(const std::string& file_path, const std::string& file_id);
    std::vector<uint8_t> decrypt_file(const std::string& file_path, const std::string& file_id);
    bool secure_delete(const std::string& file_path);
    
    // Password hashing
    std::string hash_password(const std::string& password);
    bool verify_password(const std::string& password, const std::string& password_hash);
    
    // Vault state
    bool is_unlocked() const { return is_unlocked_; }
    void lock();
    
private:
    CryptoManager() = default;
    CryptoManager(const CryptoManager&) = delete;
    CryptoManager& operator=(const CryptoManager&) = delete;
    
    std::vector<uint8_t> master_key_;
    bool is_unlocked_ = false;
    std::string master_key_file_;
    std::string vault_dir_;
    
    // Argon2 parameters
    int argon2_time_cost_ = 3;
    int argon2_memory_cost_ = 65536;
    int argon2_parallelism_ = 1;
    int file_key_size_ = 32;
    
    // Helper methods
    std::vector<uint8_t> derive_key_from_password(const std::string& password, const std::vector<uint8_t>& salt);
    std::vector<uint8_t> generate_salt(size_t length = 32);
    std::vector<uint8_t> generate_nonce(size_t length = 12);
    
    // ChaCha20-Poly1305 encryption/decryption
    std::vector<uint8_t> encrypt_data(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key, const std::vector<uint8_t>& nonce);
    std::vector<uint8_t> decrypt_data(const std::vector<uint8_t>& encrypted_data, const std::vector<uint8_t>& key, const std::vector<uint8_t>& nonce);
    
    // HKDF for key derivation
    std::vector<uint8_t> hkdf_derive(const std::vector<uint8_t>& key, const std::string& info, size_t length);
    
    // Utility methods
    std::string bytes_to_hex(const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> hex_to_bytes(const std::string& hex);
};

} // namespace vaultusb
