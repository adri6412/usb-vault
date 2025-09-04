#include "crypto.h"
#include "config.h"
#include <iostream>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <openssl/evp.h>
#include <openssl/hkdf.h>
#include <openssl/rand.h>
#include <argon2.h>

namespace vaultusb {

CryptoManager& CryptoManager::instance() {
    static CryptoManager instance;
    return instance;
}

CryptoManager::CryptoManager() {
    master_key_file_ = Config::instance().master_key_file();
    vault_dir_ = Config::instance().vault_dir();
    argon2_time_cost_ = Config::instance().argon2_time_cost();
    argon2_memory_cost_ = Config::instance().argon2_memory_cost();
    argon2_parallelism_ = Config::instance().argon2_parallelism();
    file_key_size_ = Config::instance().file_key_size();
}

std::vector<uint8_t> CryptoManager::generate_master_key() {
    std::vector<uint8_t> key(32);
    RAND_bytes(key.data(), key.size());
    return key;
}

std::string CryptoManager::seal_master_key(const std::vector<uint8_t>& master_key, const std::string& password) {
    auto salt = generate_salt();
    auto derived_key = derive_key_from_password(password, salt);
    auto nonce = generate_nonce();
    
    auto encrypted = encrypt_data(master_key, derived_key, nonce);
    
    // Simple JSON-like format
    std::ostringstream json;
    json << "{\"salt\":\"";
    for (auto b : salt) json << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    json << "\",\"nonce\":\"";
    for (auto b : nonce) json << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    json << "\",\"data\":\"";
    for (auto b : encrypted) json << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    json << "\"}";
    
    return json.str();
}

std::vector<uint8_t> CryptoManager::unseal_master_key(const std::string& sealed_data, const std::string& password) {
    // Simple JSON parsing (in production, use a proper JSON library)
    std::string salt_hex, nonce_hex, data_hex;
    
    // Extract hex strings from JSON
    size_t salt_start = sealed_data.find("\"salt\":\"") + 8;
    size_t salt_end = sealed_data.find("\"", salt_start);
    salt_hex = sealed_data.substr(salt_start, salt_end - salt_start);
    
    size_t nonce_start = sealed_data.find("\"nonce\":\"") + 9;
    size_t nonce_end = sealed_data.find("\"", nonce_start);
    nonce_hex = sealed_data.substr(nonce_start, nonce_end - nonce_start);
    
    size_t data_start = sealed_data.find("\"data\":\"") + 8;
    size_t data_end = sealed_data.find("\"", data_start);
    data_hex = sealed_data.substr(data_start, data_end - data_start);
    
    // Convert hex to bytes
    auto salt = hex_to_bytes(salt_hex);
    auto nonce = hex_to_bytes(nonce_hex);
    auto encrypted = hex_to_bytes(data_hex);
    
    auto derived_key = derive_key_from_password(password, salt);
    return decrypt_data(encrypted, derived_key, nonce);
}

bool CryptoManager::load_master_key(const std::string& password) {
    std::ifstream file(master_key_file_);
    if (!file.is_open()) {
        return false;
    }
    
    std::string sealed_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    try {
        master_key_ = unseal_master_key(sealed_data, password);
        is_unlocked_ = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load master key: " << e.what() << std::endl;
        return false;
    }
}

bool CryptoManager::save_master_key(const std::vector<uint8_t>& master_key, const std::string& password) {
    try {
        std::string sealed_data = seal_master_key(master_key, password);
        
        // Ensure directory exists
        std::string dir = master_key_file_.substr(0, master_key_file_.find_last_of('/'));
        system(("mkdir -p " + dir).c_str());
        
        std::ofstream file(master_key_file_);
        if (!file.is_open()) {
            return false;
        }
        
        file << sealed_data;
        file.close();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save master key: " << e.what() << std::endl;
        return false;
    }
}

std::vector<uint8_t> CryptoManager::derive_file_key(const std::string& file_id) {
    if (!is_unlocked_ || master_key_.empty()) {
        throw std::runtime_error("Master key not unlocked");
    }
    
    return hkdf_derive(master_key_, file_id, file_key_size_);
}

bool CryptoManager::encrypt_file(const std::string& file_path, const std::string& file_id) {
    if (!is_unlocked_ || master_key_.empty()) {
        throw std::runtime_error("Master key not unlocked");
    }
    
    try {
        auto file_key = derive_file_key(file_id);
        auto nonce = generate_nonce();
        
        // Read file
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        std::vector<uint8_t> plaintext((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        // Encrypt
        auto encrypted = encrypt_data(plaintext, file_key, nonce);
        
        // Write back (nonce + encrypted data)
        std::ofstream out_file(file_path, std::ios::binary);
        if (!out_file.is_open()) {
            return false;
        }
        
        out_file.write(reinterpret_cast<const char*>(nonce.data()), nonce.size());
        out_file.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
        out_file.close();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to encrypt file: " << e.what() << std::endl;
        return false;
    }
}

std::vector<uint8_t> CryptoManager::decrypt_file(const std::string& file_path, const std::string& file_id) {
    if (!is_unlocked_ || master_key_.empty()) {
        throw std::runtime_error("Master key not unlocked");
    }
    
    try {
        auto file_key = derive_file_key(file_id);
        
        // Read file
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file");
        }
        
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        if (data.size() < 12) {
            throw std::runtime_error("File too small");
        }
        
        // Split nonce and encrypted data
        std::vector<uint8_t> nonce(data.begin(), data.begin() + 12);
        std::vector<uint8_t> encrypted(data.begin() + 12, data.end());
        
        return decrypt_data(encrypted, file_key, nonce);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to decrypt file: ") + e.what());
    }
}

std::string CryptoManager::hash_password(const std::string& password) {
    std::vector<uint8_t> salt = generate_salt(16);
    std::vector<uint8_t> hash(32);
    
    int result = argon2i_hash_raw(
        argon2_time_cost_, argon2_memory_cost_, argon2_parallelism_,
        password.c_str(), password.length(),
        salt.data(), salt.size(),
        hash.data(), hash.size()
    );
    
    if (result != ARGON2_OK) {
        throw std::runtime_error("Argon2 hash failed");
    }
    
    // Combine salt and hash
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), salt.begin(), salt.end());
    combined.insert(combined.end(), hash.begin(), hash.end());
    
    return bytes_to_hex(combined);
}

bool CryptoManager::verify_password(const std::string& password, const std::string& password_hash) {
    try {
        auto combined = hex_to_bytes(password_hash);
        if (combined.size() < 16) {
            return false;
        }
        
        std::vector<uint8_t> salt(combined.begin(), combined.begin() + 16);
        std::vector<uint8_t> stored_hash(combined.begin() + 16, combined.end());
        std::vector<uint8_t> computed_hash(32);
        
        int result = argon2i_verify(
            stored_hash.data(), stored_hash.size(),
            password.c_str(), password.length(),
            salt.data(), salt.size()
        );
        
        return result == ARGON2_OK;
    } catch (const std::exception&) {
        return false;
    }
}

void CryptoManager::lock() {
    master_key_.clear();
    is_unlocked_ = false;
}

std::vector<uint8_t> CryptoManager::derive_key_from_password(const std::string& password, const std::vector<uint8_t>& salt) {
    std::vector<uint8_t> key(32);
    int result = argon2i_hash_raw(
        argon2_time_cost_, argon2_memory_cost_, argon2_parallelism_,
        password.c_str(), password.length(),
        salt.data(), salt.size(),
        key.data(), key.size()
    );
    
    if (result != ARGON2_OK) {
        throw std::runtime_error("Argon2 key derivation failed");
    }
    
    return key;
}

std::vector<uint8_t> CryptoManager::generate_salt(size_t length) {
    std::vector<uint8_t> salt(length);
    RAND_bytes(salt.data(), salt.size());
    return salt;
}

std::vector<uint8_t> CryptoManager::generate_nonce(size_t length) {
    std::vector<uint8_t> nonce(length);
    RAND_bytes(nonce.data(), nonce.size());
    return nonce;
}

std::vector<uint8_t> CryptoManager::encrypt_data(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key, const std::vector<uint8_t>& nonce) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }
    
    if (EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, key.data(), nonce.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize encryption");
    }
    
    std::vector<uint8_t> ciphertext(data.size() + 16); // Extra space for tag
    int len;
    int ciphertext_len;
    
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, data.data(), data.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to encrypt data");
    }
    ciphertext_len = len;
    
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize encryption");
    }
    ciphertext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    ciphertext.resize(ciphertext_len);
    return ciphertext;
}

std::vector<uint8_t> CryptoManager::decrypt_data(const std::vector<uint8_t>& encrypted_data, const std::vector<uint8_t>& key, const std::vector<uint8_t>& nonce) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }
    
    if (EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, key.data(), nonce.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize decryption");
    }
    
    std::vector<uint8_t> plaintext(encrypted_data.size());
    int len;
    int plaintext_len;
    
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, encrypted_data.data(), encrypted_data.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to decrypt data");
    }
    plaintext_len = len;
    
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize decryption");
    }
    plaintext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(plaintext_len);
    return plaintext;
}

std::vector<uint8_t> CryptoManager::hkdf_derive(const std::vector<uint8_t>& key, const std::string& info, size_t length) {
    std::vector<uint8_t> derived_key(length);
    
    if (HKDF(derived_key.data(), length, EVP_sha256(), key.data(), key.size(),
             reinterpret_cast<const uint8_t*>("vaultusb_file_key"), 16,
             reinterpret_cast<const uint8_t*>(info.c_str()), info.length()) != 1) {
        throw std::runtime_error("HKDF derivation failed");
    }
    
    return derived_key;
}

std::string CryptoManager::bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    for (auto b : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    }
    return oss.str();
}

std::vector<uint8_t> CryptoManager::hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        bytes.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
    }
    return bytes;
}

} // namespace vaultusb
