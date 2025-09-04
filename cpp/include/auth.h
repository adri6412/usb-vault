#pragma once

#include "models.h"
#include "database.h"
#include "crypto.h"
#include <string>
#include <memory>
#include <map>

namespace vaultusb {

class AuthManager {
public:
    static AuthManager& instance();
    
    // Authentication
    std::shared_ptr<User> authenticate_user(const std::string& username, const std::string& password);
    std::string create_session(const User& user, const std::string& ip_address = "", const std::string& user_agent = "");
    std::shared_ptr<User> verify_session(const std::string& token);
    bool invalidate_session(const std::string& token);
    
    // Password management
    bool change_password(User& user, const std::string& current_password, const std::string& new_password);
    std::string hash_password(const std::string& password);
    bool verify_password(const std::string& password, const std::string& password_hash);
    
    // TOTP management
    std::string setup_totp(User& user, const std::string& password);
    bool verify_totp(const User& user, const std::string& token);
    bool enable_totp(User& user, const std::string& token);
    bool disable_totp(User& user, const std::string& password);
    
    // Session management
    void cleanup_expired_sessions();
    
private:
    AuthManager() = default;
    AuthManager(const AuthManager&) = delete;
    AuthManager& operator=(const AuthManager&) = delete;
    
    std::string secret_key_;
    int idle_timeout_ = 600;
    
    // JWT-like token management
    std::string create_token(const std::map<std::string, std::string>& payload);
    std::map<std::string, std::string> parse_token(const std::string& token);
    bool is_token_valid(const std::string& token);
    
    // TOTP implementation
    std::string generate_totp_secret();
    std::string generate_totp_qr_url(const std::string& secret, const std::string& username);
    bool verify_totp_token(const std::string& secret, const std::string& token);
    
    // Base32 encoding/decoding for TOTP
    std::string base32_encode(const std::vector<uint8_t>& data);
    std::vector<uint8_t> base32_decode(const std::string& encoded);
    
    // HMAC-SHA1 for TOTP
    std::vector<uint8_t> hmac_sha1(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data);
    
    // Time-based functions
    uint64_t get_current_time_step();
    std::string time_step_to_totp(const std::string& secret, uint64_t time_step);
};

} // namespace vaultusb
