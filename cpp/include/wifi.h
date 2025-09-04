#pragma once

#include "models.h"
#include "database.h"
#include <string>
#include <vector>
#include <memory>

namespace vaultusb {

class WiFiManager {
public:
    static WiFiManager& instance();
    
    // Network scanning and connection
    std::vector<WiFiNetwork> scan_networks();
    WiFiStatus get_status();
    std::pair<bool, std::string> connect(const std::string& ssid, const std::string& password = "", const std::string& security = "WPA2");
    std::pair<bool, std::string> disconnect();
    std::pair<bool, std::string> forget_network(const std::string& ssid);
    
    // Saved networks
    std::vector<std::string> get_saved_networks();
    
private:
    WiFiManager() = default;
    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;
    
    std::string wpa_cli_path_ = "/sbin/wpa_cli";
    std::string interface_ = "wlan0";
    
    // Helper methods
    std::pair<bool, std::string> run_wpa_cli(const std::string& command, const std::string& interface = "");
    std::vector<WiFiNetwork> parse_scan_results(const std::string& output);
    WiFiStatus parse_status_output(const std::string& output);
    std::string sanitize_ssid(const std::string& ssid);
    std::string sanitize_password(const std::string& password);
    
    // Security type detection
    std::string determine_security_type(const std::vector<std::string>& flags);
    
    // Logging
    void log_event(const std::string& level, const std::string& message, const std::string& component);
};

} // namespace vaultusb
