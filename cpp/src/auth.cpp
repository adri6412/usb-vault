#include "auth.h"
#include "config.h"
#include "database.h"
#include "crypto.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <ctime>

namespace vaultusb {

AuthManager& AuthManager::instance() {
    static AuthManager instance;
    return instance;
}

AuthManager::AuthManager() {
    secret_key_ = Config::instance().secret_key();
    idle_timeout_ = Config::instance().idle_timeout();
}

std::shared_ptr<User> AuthManager::authenticate_user(const std::string& username, const std::string& password) {
    auto user = Database::instance().get_user_by_username(username);
    if (!user) {
        return nullptr;
    }
    
    if (CryptoManager::instance().verify_password(password, user->password_hash)) {
        user->last_login = std::time(nullptr);
        Database::instance().update_user(*user);
        return user;
    }
    
    return nullptr;
}

std::string AuthManager::create_session(const User& user, const std::string& ip_address, const std::string& user_agent) {
    Session session(user.id, ip_address, user_agent);
    session.id = generate_session_id();
    
    if (!Database::instance().create_session(session)) {
        return "";
    }
    
    // Create simple token (in production, use JWT)
    std::ostringstream token;
    token << "vaultusb:" << user.id << ":" << session.id << ":" << std::time(nullptr);
    return token.str();
}

std::shared_ptr<User> AuthManager::verify_session(const std::string& token) {
    // Parse simple token
    std::istringstream iss(token);
    std::string prefix, user_id_str, session_id, timestamp_str;
    
    if (!std::getline(iss, prefix, ':') || prefix != "vaultusb") {
        return nullptr;
    }
    if (!std::getline(iss, user_id_str, ':')) {
        return nullptr;
    }
    if (!std::getline(iss, session_id, ':')) {
        return nullptr;
    }
    if (!std::getline(iss, timestamp_str, ':')) {
        return nullptr;
    }
    
    int user_id = std::stoi(user_id_str);
    std::time_t token_time = std::stol(timestamp_str);
    
    // Check if token is expired
    if (std::time(nullptr) - token_time > idle_timeout_) {
        return nullptr;
    }
    
    // Verify session exists and is active
    auto session = Database::instance().get_session_by_id(session_id);
    if (!session || session->user_id != user_id || !session->is_active) {
        return nullptr;
    }
    
    // Update last activity
    session->last_activity = std::time(nullptr);
    Database::instance().update_session(*session);
    
    return Database::instance().get_user_by_id(user_id);
}

bool AuthManager::invalidate_session(const std::string& token) {
    // Parse token to get session ID
    std::istringstream iss(token);
    std::string prefix, user_id_str, session_id, timestamp_str;
    
    if (!std::getline(iss, prefix, ':') || prefix != "vaultusb") {
        return false;
    }
    if (!std::getline(iss, user_id_str, ':')) {
        return false;
    }
    if (!std::getline(iss, session_id, ':')) {
        return false;
    }
    
    auto session = Database::instance().get_session_by_id(session_id);
    if (!session) {
        return false;
    }
    
    session->is_active = false;
    return Database::instance().update_session(*session);
}

bool AuthManager::change_password(User& user, const std::string& current_password, const std::string& new_password) {
    if (!verify_password(current_password, user.password_hash)) {
        return false;
    }
    
    user.password_hash = hash_password(new_password);
    return Database::instance().update_user(user);
}

std::string AuthManager::hash_password(const std::string& password) {
    return CryptoManager::instance().hash_password(password);
}

bool AuthManager::verify_password(const std::string& password, const std::string& password_hash) {
    return CryptoManager::instance().verify_password(password, password_hash);
}

std::string AuthManager::setup_totp(User& user, const std::string& password) {
    if (!verify_password(password, user.password_hash)) {
        return "";
    }
    
    user.totp_secret = generate_totp_secret();
    user.totp_enabled = false;
    Database::instance().update_user(user);
    
    return generate_totp_qr_url(user.totp_secret, user.username);
}

bool AuthManager::verify_totp(const User& user, const std::string& token) {
    if (user.totp_secret.empty() || !user.totp_enabled) {
        return false;
    }
    
    return verify_totp_token(user.totp_secret, token);
}

bool AuthManager::enable_totp(User& user, const std::string& token) {
    if (!verify_totp(user, token)) {
        return false;
    }
    
    user.totp_enabled = true;
    return Database::instance().update_user(user);
}

bool AuthManager::disable_totp(User& user, const std::string& password) {
    if (!verify_password(password, user.password_hash)) {
        return false;
    }
    
    user.totp_secret = "";
    user.totp_enabled = false;
    return Database::instance().update_user(user);
}

void AuthManager::cleanup_expired_sessions() {
    Database::instance().cleanup_expired_sessions(idle_timeout_);
}

std::string AuthManager::generate_session_id() {
    std::ostringstream oss;
    oss << std::hex << std::time(nullptr) << std::rand();
    return oss.str();
}

std::string AuthManager::generate_totp_secret() {
    // Generate 20 random bytes and encode as base32
    std::vector<uint8_t> secret(20);
    for (auto& b : secret) {
        b = std::rand() % 256;
    }
    return base32_encode(secret);
}

std::string AuthManager::generate_totp_qr_url(const std::string& secret, const std::string& username) {
    std::ostringstream url;
    url << "otpauth://totp/VaultUSB:" << username << "?secret=" << secret << "&issuer=VaultUSB";
    return url.str();
}

bool AuthManager::verify_totp_token(const std::string& secret, const std::string& token) {
    try {
        auto secret_bytes = base32_decode(secret);
        uint64_t time_step = get_current_time_step();
        
        // Check current and previous time step
        for (int i = -1; i <= 1; i++) {
            std::string expected_token = time_step_to_totp(secret, time_step + i);
            if (expected_token == token) {
                return true;
            }
        }
        return false;
    } catch (const std::exception&) {
        return false;
    }
}

std::string AuthManager::base32_encode(const std::vector<uint8_t>& data) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::string result;
    
    for (size_t i = 0; i < data.size(); i += 5) {
        uint64_t buffer = 0;
        int bits = 0;
        
        for (int j = 0; j < 5 && i + j < data.size(); j++) {
            buffer = (buffer << 8) | data[i + j];
            bits += 8;
        }
        
        while (bits >= 5) {
            result += chars[(buffer >> (bits - 5)) & 0x1F];
            bits -= 5;
        }
        
        if (bits > 0) {
            result += chars[(buffer << (5 - bits)) & 0x1F];
        }
    }
    
    return result;
}

std::vector<uint8_t> AuthManager::base32_decode(const std::string& encoded) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::vector<uint8_t> result;
    
    uint64_t buffer = 0;
    int bits = 0;
    
    for (char c : encoded) {
        size_t pos = chars.find(std::toupper(c));
        if (pos == std::string::npos) continue;
        
        buffer = (buffer << 5) | pos;
        bits += 5;
        
        while (bits >= 8) {
            result.push_back((buffer >> (bits - 8)) & 0xFF);
            bits -= 8;
        }
    }
    
    return result;
}

std::vector<uint8_t> AuthManager::hmac_sha1(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result(20);
    unsigned int len;
    
    HMAC(EVP_sha1(), key.data(), key.size(), data.data(), data.size(), result.data(), &len);
    result.resize(len);
    
    return result;
}

uint64_t AuthManager::get_current_time_step() {
    return std::time(nullptr) / 30; // 30-second time steps
}

std::string AuthManager::time_step_to_totp(const std::string& secret, uint64_t time_step) {
    auto secret_bytes = base32_decode(secret);
    
    // Convert time step to big-endian bytes
    std::vector<uint8_t> time_bytes(8);
    for (int i = 7; i >= 0; i--) {
        time_bytes[i] = time_step & 0xFF;
        time_step >>= 8;
    }
    
    // Compute HMAC-SHA1
    auto hmac = hmac_sha1(secret_bytes, time_bytes);
    
    // Dynamic truncation
    int offset = hmac[19] & 0x0F;
    int code = ((hmac[offset] & 0x7F) << 24) |
               ((hmac[offset + 1] & 0xFF) << 16) |
               ((hmac[offset + 2] & 0xFF) << 8) |
               (hmac[offset + 3] & 0xFF);
    
    code %= 1000000; // 6-digit code
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(6) << code;
    return oss.str();
}

} // namespace vaultusb
