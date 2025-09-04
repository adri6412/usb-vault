#include "wifi.h"
#include "database.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <map>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace vaultusb {

WiFiManager& WiFiManager::instance() {
    static WiFiManager instance;
    return instance;
}

std::vector<WiFiNetwork> WiFiManager::scan_networks() {
    try {
        auto [success, output] = run_wpa_cli("scan");
        if (!success) {
            log_event("ERROR", "Failed to start Wi-Fi scan", "wifi");
            return {};
        }
        
        // Wait for scan to complete
        sleep(5);
        
        auto [scan_success, scan_output] = run_wpa_cli("scan_results");
        if (!scan_success) {
            log_event("ERROR", "Failed to get scan results", "wifi");
            return {};
        }
        
        auto networks = parse_scan_results(scan_output);
        log_event("INFO", "Scanned for Wi-Fi networks, found " + std::to_string(networks.size()), "wifi");
        
        return networks;
    } catch (const std::exception& e) {
        log_event("ERROR", "Wi-Fi scan failed: " + std::string(e.what()), "wifi");
        return {};
    }
}

WiFiStatus WiFiManager::get_status() {
    try {
        auto [success, output] = run_wpa_cli("status");
        if (!success) {
            return WiFiStatus(interface_, "error");
        }
        
        return parse_status_output(output);
    } catch (const std::exception& e) {
        log_event("ERROR", "Failed to get Wi-Fi status: " + std::string(e.what()), "wifi");
        return WiFiStatus(interface_, "error");
    }
}

std::pair<bool, std::string> WiFiManager::connect(const std::string& ssid, const std::string& password, const std::string& security) {
    try {
        // Sanitize inputs
        if (ssid.length() > 32) {
            return {false, "SSID too long"};
        }
        
        if (!password.empty() && password.length() < 8) {
            return {false, "Password too short"};
        }
        
        // Disconnect first
        run_wpa_cli("disconnect");
        sleep(1);
        
        // Remove all networks
        auto [list_success, list_output] = run_wpa_cli("list_networks");
        if (list_success) {
            std::istringstream iss(list_output);
            std::string line;
            std::getline(iss, line); // Skip header
            
            while (std::getline(iss, line)) {
                if (!line.empty()) {
                    std::istringstream line_stream(line);
                    std::string network_id;
                    line_stream >> network_id;
                    if (!network_id.empty()) {
                        run_wpa_cli("remove_network " + network_id);
                    }
                }
            }
        }
        
        // Add new network
        auto [add_success, add_output] = run_wpa_cli("add_network");
        if (!add_success) {
            return {false, "Failed to add network"};
        }
        
        std::string network_id = add_output;
        network_id.erase(network_id.find_last_not_of(" \n\r\t") + 1);
        
        // Set SSID
        auto [ssid_success, ssid_output] = run_wpa_cli("set_network " + network_id + " ssid \"" + ssid + "\"");
        if (!ssid_success) {
            return {false, "Failed to set SSID"};
        }
        
        // Set security
        if (security == "Open") {
            run_wpa_cli("set_network " + network_id + " key_mgmt NONE");
        } else if (security == "WPA" || security == "WPA2") {
            run_wpa_cli("set_network " + network_id + " key_mgmt WPA-PSK");
            if (!password.empty()) {
                run_wpa_cli("set_network " + network_id + " psk \"" + password + "\"");
            } else {
                return {false, "Password required for WPA/WPA2"};
            }
        } else if (security == "WEP") {
            run_wpa_cli("set_network " + network_id + " key_mgmt NONE");
            if (!password.empty()) {
                run_wpa_cli("set_network " + network_id + " wep_key0 \"" + password + "\"");
            } else {
                return {false, "Password required for WEP"};
            }
        }
        
        // Enable network
        auto [enable_success, enable_output] = run_wpa_cli("enable_network " + network_id);
        if (!enable_success) {
            return {false, "Failed to enable network"};
        }
        
        // Select network
        auto [select_success, select_output] = run_wpa_cli("select_network " + network_id);
        if (!select_success) {
            return {false, "Failed to select network"};
        }
        
        // Save configuration
        run_wpa_cli("save_config");
        
        // Wait for connection
        for (int i = 0; i < 30; i++) {
            sleep(1);
            auto status = get_status();
            if (status.status == "connected") {
                log_event("INFO", "Connected to Wi-Fi network: " + ssid, "wifi");
                return {true, "Connected successfully"};
            } else if (status.status == "disconnected") {
                auto [status_success, status_output] = run_wpa_cli("status");
                if (status_success && (status_output.find("FAILED") != std::string::npos || 
                                      status_output.find("DISCONNECTED") != std::string::npos)) {
                    return {false, "Connection failed"};
                }
            }
        }
        
        return {false, "Connection timeout"};
    } catch (const std::exception& e) {
        std::string error_msg = "Connection error: " + std::string(e.what());
        log_event("ERROR", error_msg, "wifi");
        return {false, error_msg};
    }
}

std::pair<bool, std::string> WiFiManager::disconnect() {
    try {
        auto [success, output] = run_wpa_cli("disconnect");
        if (success) {
            log_event("INFO", "Disconnected from Wi-Fi network", "wifi");
            return {true, "Disconnected successfully"};
        } else {
            return {false, "Failed to disconnect"};
        }
    } catch (const std::exception& e) {
        std::string error_msg = "Disconnect error: " + std::string(e.what());
        log_event("ERROR", error_msg, "wifi");
        return {false, error_msg};
    }
}

std::pair<bool, std::string> WiFiManager::forget_network(const std::string& ssid) {
    try {
        auto [success, output] = run_wpa_cli("list_networks");
        if (!success) {
            return {false, "Failed to list networks"};
        }
        
        std::string network_id;
        std::istringstream iss(output);
        std::string line;
        std::getline(iss, line); // Skip header
        
        while (std::getline(iss, line)) {
            if (!line.empty()) {
                std::istringstream line_stream(line);
                std::string id, name;
                line_stream >> id >> name;
                if (name == ssid) {
                    network_id = id;
                    break;
                }
            }
        }
        
        if (network_id.empty()) {
            return {false, "Network not found"};
        }
        
        auto [remove_success, remove_output] = run_wpa_cli("remove_network " + network_id);
        if (remove_success) {
            run_wpa_cli("save_config");
            log_event("INFO", "Forgot Wi-Fi network: " + ssid, "wifi");
            return {true, "Network forgotten"};
        } else {
            return {false, "Failed to forget network"};
        }
    } catch (const std::exception& e) {
        std::string error_msg = "Forget network error: " + std::string(e.what());
        log_event("ERROR", error_msg, "wifi");
        return {false, error_msg};
    }
}

std::vector<std::string> WiFiManager::get_saved_networks() {
    try {
        auto [success, output] = run_wpa_cli("list_networks");
        if (!success) {
            return {};
        }
        
        std::vector<std::string> networks;
        std::istringstream iss(output);
        std::string line;
        std::getline(iss, line); // Skip header
        
        while (std::getline(iss, line)) {
            if (!line.empty()) {
                std::istringstream line_stream(line);
                std::string id, name;
                line_stream >> id >> name;
                if (!name.empty()) {
                    networks.push_back(name);
                }
            }
        }
        
        return networks;
    } catch (const std::exception& e) {
        log_event("ERROR", "Failed to get saved networks: " + std::string(e.what()), "wifi");
        return {};
    }
}

std::pair<bool, std::string> WiFiManager::run_wpa_cli(const std::string& command, const std::string& interface) {
    std::string iface = interface.empty() ? interface_ : interface;
    std::string cmd = wpa_cli_path_ + " -i " + iface + " " + command;
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {false, "Failed to execute command"};
    }
    
    std::string output;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    
    int status = pclose(pipe);
    return {status == 0, output};
}

std::vector<WiFiNetwork> WiFiManager::parse_scan_results(const std::string& output) {
    std::vector<WiFiNetwork> networks;
    std::istringstream iss(output);
    std::string line;
    std::getline(iss, line); // Skip header
    
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        
        std::istringstream line_stream(line);
        std::string bssid, frequency_str, signal_str, flags_str, ssid;
        
        if (line_stream >> bssid >> frequency_str >> signal_str >> flags_str) {
            // Get SSID (may contain spaces)
            std::getline(line_stream, ssid);
            ssid.erase(0, ssid.find_first_not_of(" \t"));
            
            if (ssid.empty()) continue;
            
            int frequency = 0;
            int signal_level = -100;
            
            try {
                frequency = std::stoi(frequency_str);
                signal_level = std::stoi(signal_str);
            } catch (const std::exception&) {
                continue;
            }
            
            // Parse flags
            std::vector<std::string> flags;
            std::istringstream flags_stream(flags_str);
            std::string flag;
            while (flags_stream >> flag) {
                flags.push_back(flag);
            }
            
            std::string security = determine_security_type(flags);
            
            networks.emplace_back(ssid, bssid, frequency, signal_level, security, flags);
        }
    }
    
    // Remove duplicates and sort by signal strength
    std::map<std::string, WiFiNetwork> unique_networks;
    for (const auto& network : networks) {
        if (unique_networks.find(network.ssid) == unique_networks.end() || 
            network.signal_level > unique_networks[network.ssid].signal_level) {
            unique_networks[network.ssid] = network;
        }
    }
    
    std::vector<WiFiNetwork> result;
    for (const auto& pair : unique_networks) {
        result.push_back(pair.second);
    }
    
    std::sort(result.begin(), result.end(), 
              [](const WiFiNetwork& a, const WiFiNetwork& b) {
                  return a.signal_level > b.signal_level;
              });
    
    return result;
}

WiFiStatus WiFiManager::parse_status_output(const std::string& output) {
    WiFiStatus status(interface_, "disconnected");
    
    std::istringstream iss(output);
    std::string line;
    
    while (std::getline(iss, line)) {
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        
        if (key == "wpa_state") {
            if (value == "COMPLETED") {
                status.status = "connected";
            }
        } else if (key == "ssid") {
            status.ssid = value;
        } else if (key == "ip_address") {
            status.ip_address = value;
        }
    }
    
    if (status.status == "connected") {
        // Get signal level
        auto [success, signal_output] = run_wpa_cli("signal_poll");
        if (success) {
            std::istringstream signal_stream(signal_output);
            std::string signal_line;
            while (std::getline(signal_stream, signal_line)) {
                if (signal_line.find("RSSI=") != std::string::npos) {
                    try {
                        size_t start = signal_line.find("RSSI=") + 5;
                        size_t end = signal_line.find_first_not_of("-0123456789", start);
                        std::string rssi_str = signal_line.substr(start, end - start);
                        status.signal_level = std::stoi(rssi_str);
                    } catch (const std::exception&) {
                        // Ignore parsing errors
                    }
                    break;
                }
            }
        }
    }
    
    return status;
}

std::string WiFiManager::determine_security_type(const std::vector<std::string>& flags) {
    for (const auto& flag : flags) {
        if (flag == "WPA2") return "WPA2";
        if (flag == "WPA") return "WPA";
        if (flag == "WEP") return "WEP";
    }
    return "Open";
}

void WiFiManager::log_event(const std::string& level, const std::string& message, const std::string& component) {
    SystemLog log(level, message, component);
    Database::instance().log_event(log);
}

} // namespace vaultusb
