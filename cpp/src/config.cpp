#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace vaultusb {

Config& Config::instance() {
    static Config instance;
    return instance;
}

void Config::load_from_file(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        set_defaults();
        return;
    }
    
    std::string line;
    std::string current_section;
    
    while (std::getline(file, line)) {
        // Remove comments
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty()) continue;
        
        // Check for section headers [section]
        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.length() - 2);
            continue;
        }
        
        // Parse key = value
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);
            
            // Trim key and value
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // Remove quotes if present
            if (value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }
            
            // Store with section prefix
            std::string full_key = current_section.empty() ? key : current_section + "." + key;
            config_map_[full_key] = value;
        }
    }
    
    // Update member variables
    app_name_ = get_value("app.name", app_name_);
    app_version_ = get_value("app.version", app_version_);
    debug_ = get_bool_value("app.debug", debug_);
    host_ = get_value("app.host", host_);
    port_ = get_int_value("app.port", port_);
    secret_key_ = get_value("app.secret_key", secret_key_);
    
    usb0_ip_ = get_value("networking.usb0_ip", usb0_ip_);
    usb0_netmask_ = get_value("networking.usb0_netmask", usb0_netmask_);
    usb0_dhcp_range_ = get_value("networking.usb0_dhcp_range", usb0_dhcp_range_);
    uap0_ip_ = get_value("networking.uap0_ip", uap0_ip_);
    uap0_netmask_ = get_value("networking.uap0_netmask", uap0_netmask_);
    uap0_dhcp_range_ = get_value("networking.uap0_dhcp_range", uap0_dhcp_range_);
    ap_ssid_ = get_value("networking.ap_ssid", ap_ssid_);
    ap_password_ = get_value("networking.ap_password", ap_password_);
    
    idle_timeout_ = get_int_value("security.idle_timeout", idle_timeout_);
    master_key_file_ = get_value("security.master_key_file", master_key_file_);
    vault_dir_ = get_value("security.vault_dir", vault_dir_);
    db_file_ = get_value("security.db_file", db_file_);
    argon2_time_cost_ = get_int_value("security.argon2_time_cost", argon2_time_cost_);
    argon2_memory_cost_ = get_int_value("security.argon2_memory_cost", argon2_memory_cost_);
    argon2_parallelism_ = get_int_value("security.argon2_parallelism", argon2_parallelism_);
    file_key_size_ = get_int_value("security.file_key_size", file_key_size_);
    
    tls_enabled_ = get_bool_value("tls.enabled", tls_enabled_);
    cert_file_ = get_value("tls.cert_file", cert_file_);
    key_file_ = get_value("tls.key_file", key_file_);
    
    sudoers_file_ = get_value("system.sudoers_file", sudoers_file_);
    rpi_update_enabled_ = get_bool_value("system.rpi_update_enabled", rpi_update_enabled_);
    dietpi_optimized_ = get_bool_value("system.dietpi_optimized", dietpi_optimized_);
    dietpi_version_ = get_value("dietpi.version", dietpi_version_);
    python_version_ = get_value("dietpi.python_version", python_version_);
    debian_version_ = get_value("dietpi.debian_version", debian_version_);
}

void Config::set_defaults() {
    // Defaults are already set in member variables
}

std::string Config::get_value(const std::string& key, const std::string& default_value) const {
    auto it = config_map_.find(key);
    return (it != config_map_.end()) ? it->second : default_value;
}

int Config::get_int_value(const std::string& key, int default_value) const {
    auto it = config_map_.find(key);
    if (it != config_map_.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception&) {
            return default_value;
        }
    }
    return default_value;
}

bool Config::get_bool_value(const std::string& key, bool default_value) const {
    auto it = config_map_.find(key);
    if (it != config_map_.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return (value == "true" || value == "1" || value == "yes" || value == "on");
    }
    return default_value;
}

} // namespace vaultusb
