#pragma once

#include <string>
#include <ctime>
#include <vector>
#include <memory>

namespace vaultusb {

struct User {
    int id = 0;
    std::string username;
    std::string password_hash;
    std::string totp_secret;
    bool totp_enabled = false;
    std::time_t created_at = 0;
    std::time_t last_login = 0;
    bool is_active = true;
    
    User() = default;
    User(const std::string& uname, const std::string& pwd_hash) 
        : username(uname), password_hash(pwd_hash) {
        created_at = std::time(nullptr);
    }
};

struct File {
    std::string id;
    std::string original_name;
    std::string encrypted_name;
    int size = 0;
    std::string mime_type;
    std::time_t created_at = 0;
    std::time_t modified_at = 0;
    int user_id = 0;
    bool is_deleted = false;
    
    File() = default;
    File(const std::string& file_id, const std::string& orig_name, 
         const std::string& enc_name, int file_size, const std::string& mime, int uid)
        : id(file_id), original_name(orig_name), encrypted_name(enc_name), 
          size(file_size), mime_type(mime), user_id(uid) {
        created_at = std::time(nullptr);
        modified_at = created_at;
    }
};

struct Session {
    std::string id;
    int user_id = 0;
    std::time_t created_at = 0;
    std::time_t last_activity = 0;
    bool is_active = true;
    std::string ip_address;
    std::string user_agent;
    
    Session() = default;
    Session(int uid, const std::string& ip, const std::string& ua)
        : user_id(uid), ip_address(ip), user_agent(ua) {
        created_at = std::time(nullptr);
        last_activity = created_at;
    }
};

struct WiFiNetwork {
    std::string ssid;
    std::string bssid;
    int frequency = 0;
    int signal_level = -100;
    std::string security;
    std::vector<std::string> flags;
    
    WiFiNetwork() = default;
    WiFiNetwork(const std::string& s, const std::string& b, int freq, int signal, 
                const std::string& sec, const std::vector<std::string>& f)
        : ssid(s), bssid(b), frequency(freq), signal_level(signal), security(sec), flags(f) {}
};

struct WiFiStatus {
    std::string interface = "wlan0";
    std::string status = "disconnected";
    std::string ssid;
    std::string ip_address;
    int signal_level = -100;
    
    WiFiStatus() = default;
    WiFiStatus(const std::string& iface, const std::string& stat)
        : interface(iface), status(stat) {}
};

struct SystemStatus {
    int uptime = 0;
    double memory_usage = 0.0;
    double disk_usage = 0.0;
    double cpu_usage = 0.0;
    bool reboot_required = false;
    
    SystemStatus() = default;
};

struct PackageUpdate {
    std::string package;
    std::string current_version;
    std::string available_version;
    std::string priority = "normal";
    
    PackageUpdate() = default;
    PackageUpdate(const std::string& pkg, const std::string& curr, const std::string& avail, const std::string& prio)
        : package(pkg), current_version(curr), available_version(avail), priority(prio) {}
};

struct SystemLog {
    int id = 0;
    std::string level;
    std::string message;
    std::string component;
    std::time_t created_at = 0;
    int user_id = 0;
    
    SystemLog() = default;
    SystemLog(const std::string& lvl, const std::string& msg, const std::string& comp, int uid = 0)
        : level(lvl), message(msg), component(comp), user_id(uid) {
        created_at = std::time(nullptr);
    }
};

} // namespace vaultusb
