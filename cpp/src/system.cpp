#include "system.h"
#include "config.h"
#include "database.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <sys/statvfs.h>
#include <unistd.h>

namespace vaultusb {

SystemManager& SystemManager::instance() {
    static SystemManager instance;
    return instance;
}

SystemManager::SystemManager() {
    is_dietpi_ = check_dietpi();
    sudoers_file_ = Config::instance().sudoers_file();
    setup_sudoers();
}

SystemStatus SystemManager::get_system_status() {
    try {
        SystemStatus status;
        status.uptime = get_uptime_seconds();
        status.memory_usage = get_memory_usage_percent();
        status.disk_usage = get_disk_usage_percent();
        status.cpu_usage = get_cpu_usage_percent();
        status.reboot_required = is_reboot_required();
        
        return status;
    } catch (const std::exception& e) {
        log_event("ERROR", "Failed to get system status: " + std::string(e.what()), "system");
        return SystemStatus();
    }
}

std::vector<PackageUpdate> SystemManager::check_updates() {
    try {
        // Update package lists
        auto [update_success, update_output] = execute_command_with_result("apt-get update", 300);
        if (!update_success) {
            log_event("ERROR", "apt-get update failed: " + update_output, "system");
            return {};
        }
        
        // Check for upgradable packages
        auto [check_success, check_output] = execute_command_with_result("apt-get --just-print dist-upgrade", 300);
        if (!check_success) {
            return {};
        }
        
        auto packages = parse_upgrade_output(check_output);
        log_event("INFO", "Checked for updates, found " + std::to_string(packages.size()) + " packages", "system");
        
        return packages;
    } catch (const std::exception& e) {
        log_event("ERROR", "Update check failed: " + std::string(e.what()), "system");
        return {};
    }
}

SystemManager::UpgradeResult SystemManager::upgrade_system() {
    try {
        log_event("INFO", "Starting system upgrade", "system");
        
        // Update package lists
        auto [update_success, update_output] = execute_command_with_result("apt-get update", 600);
        if (!update_success) {
            return {false, "Failed to update package lists: " + update_output, update_output};
        }
        
        // Perform full upgrade
        auto [upgrade_success, upgrade_output] = execute_command_with_result("apt-get -y full-upgrade", 1800);
        std::string upgrade_log = upgrade_output;
        
        if (!upgrade_success) {
            return {false, "Upgrade failed: " + upgrade_output, upgrade_log};
        }
        
        // Clean up
        execute_command("apt-get -y autoremove --purge", 300);
        execute_command("apt-get -y clean", 300);
        
        // Update Raspberry Pi firmware if available
        if (file_exists("/usr/bin/rpi-update")) {
            log_event("INFO", "Updating Raspberry Pi firmware", "system");
            auto [firmware_success, firmware_output] = execute_command_with_result("rpi-update", 1800);
            upgrade_log += "\n\nFirmware update:\n" + firmware_output;
        }
        
        // Update Raspberry Pi configuration if available
        if (file_exists("/usr/bin/raspi-config")) {
            log_event("INFO", "Updating Raspberry Pi configuration", "system");
            auto [config_success, config_output] = execute_command_with_result("raspi-config nonint do_update", 300);
            upgrade_log += "\n\nConfig update:\n" + config_output;
        }
        
        log_event("INFO", "System upgrade completed successfully", "system");
        
        return {true, "System upgrade completed successfully", upgrade_log};
    } catch (const std::exception& e) {
        log_event("ERROR", "System upgrade failed: " + std::string(e.what()), "system");
        return {false, "Upgrade error: " + std::string(e.what()), e.what()};
    }
}

std::pair<bool, std::string> SystemManager::reboot_system() {
    try {
        log_event("INFO", "System reboot requested", "system");
        
        // Schedule reboot in 1 minute
        auto [success, output] = execute_command_with_result("shutdown -r +1", 30);
        if (success) {
            return {true, "System will reboot in 1 minute"};
        } else {
            return {false, "Failed to schedule reboot: " + output};
        }
    } catch (const std::exception& e) {
        log_event("ERROR", "Failed to reboot system: " + std::string(e.what()), "system");
        return {false, "Reboot failed: " + std::string(e.what())};
    }
}

SystemManager::SystemInfo SystemManager::get_system_info() {
    SystemInfo info;
    
    try {
        // OS information
        if (file_exists("/etc/os-release")) {
            std::string os_release = read_file("/etc/os-release");
            std::istringstream iss(os_release);
            std::string line;
            
            while (std::getline(iss, line)) {
                size_t eq_pos = line.find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = line.substr(0, eq_pos);
                    std::string value = line.substr(eq_pos + 1);
                    // Remove quotes
                    if (value.front() == '"' && value.back() == '"') {
                        value = value.substr(1, value.length() - 2);
                    }
                    
                    if (key == "NAME") info.os_name = value;
                    else if (key == "VERSION") info.os_version = value;
                }
            }
        }
        
        // DietPi specific information
        if (is_dietpi_) {
            if (file_exists("/boot/dietpi/.dietpi_version")) {
                info.dietpi_version = read_file("/boot/dietpi/.dietpi_version");
                // Remove newline
                info.dietpi_version.erase(info.dietpi_version.find_last_not_of("\n\r") + 1);
            }
            
            if (file_exists("/boot/dietpi/.hw_model")) {
                info.dietpi_hw_model = read_file("/boot/dietpi/.hw_model");
                // Remove newline
                info.dietpi_hw_model.erase(info.dietpi_hw_model.find_last_not_of("\n\r") + 1);
            }
        }
        
        // Hardware information
        if (file_exists("/proc/cpuinfo")) {
            std::string cpuinfo = read_file("/proc/cpuinfo");
            std::istringstream iss(cpuinfo);
            std::string line;
            
            while (std::getline(iss, line)) {
                if (line.find("Model") == 0) {
                    size_t colon_pos = line.find(':');
                    if (colon_pos != std::string::npos) {
                        info.hardware_model = line.substr(colon_pos + 1);
                        // Trim whitespace
                        info.hardware_model.erase(0, info.hardware_model.find_first_not_of(" \t"));
                        info.hardware_model.erase(info.hardware_model.find_last_not_of(" \t\n\r") + 1);
                    }
                    break;
                }
            }
        }
        
        // Kernel version
        if (file_exists("/proc/version")) {
            info.kernel_version = read_file("/proc/version");
            // Remove newline
            info.kernel_version.erase(info.kernel_version.find_last_not_of("\n\r") + 1);
        }
        
    } catch (const std::exception& e) {
        log_event("ERROR", "Failed to get system info: " + std::string(e.what()), "system");
    }
    
    return info;
}

void SystemManager::setup_sudoers() {
    std::string sudoers_content;
    
    if (is_dietpi_) {
        sudoers_content = R"(# VaultUSB sudoers configuration for DietPi
vaultusb ALL=(ALL) NOPASSWD: /usr/bin/apt-get, /usr/bin/rpi-update, /usr/bin/raspi-config, /sbin/reboot, /usr/sbin/iw, /usr/sbin/wpa_cli, /usr/sbin/hostapd, /usr/sbin/dnsmasq, /usr/bin/dietpi-config
)";
    } else {
        sudoers_content = R"(# VaultUSB sudoers configuration
vaultusb ALL=(ALL) NOPASSWD: /usr/bin/apt-get, /usr/bin/rpi-update, /usr/bin/raspi-config, /sbin/reboot
)";
    }
    
    try {
        write_file(sudoers_file_, sudoers_content);
        system(("chmod 440 " + sudoers_file_).c_str());
        log_event("INFO", "Sudoers file configured", "system");
    } catch (const std::exception& e) {
        log_event("ERROR", "Failed to setup sudoers: " + std::string(e.what()), "system");
    }
}

bool SystemManager::check_dietpi() {
    return file_exists("/boot/dietpi/.dietpi_version");
}

std::vector<PackageUpdate> SystemManager::parse_upgrade_output(const std::string& output) {
    std::vector<PackageUpdate> packages;
    std::istringstream iss(output);
    std::string line;
    
    while (std::getline(iss, line)) {
        // Look for lines like: "Inst package [version] (version)"
        if (line.find("Inst ") != std::string::npos && 
            line.find('[') != std::string::npos && 
            line.find(']') != std::string::npos) {
            
            try {
                // Extract package name
                size_t inst_pos = line.find("Inst ");
                size_t space_pos = line.find(' ', inst_pos + 5);
                if (space_pos == std::string::npos) continue;
                
                std::string package_name = line.substr(inst_pos + 5, space_pos - inst_pos - 5);
                
                // Extract versions
                size_t bracket_start = line.find('[');
                size_t bracket_end = line.find(']', bracket_start);
                if (bracket_start == std::string::npos || bracket_end == std::string::npos) continue;
                
                std::string current_version = line.substr(bracket_start + 1, bracket_end - bracket_start - 1);
                
                size_t paren_start = line.find('(', bracket_end);
                size_t paren_end = line.find(')', paren_start);
                if (paren_start == std::string::npos || paren_end == std::string::npos) continue;
                
                std::string available_version = line.substr(paren_start + 1, paren_end - paren_start - 1);
                
                // Determine priority
                std::string priority = "normal";
                std::string lower_line = line;
                std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);
                if (lower_line.find("security") != std::string::npos) {
                    priority = "security";
                } else if (lower_line.find("important") != std::string::npos) {
                    priority = "important";
                }
                
                packages.emplace_back(package_name, current_version, available_version, priority);
            } catch (const std::exception&) {
                continue;
            }
        }
    }
    
    return packages;
}

std::string SystemManager::execute_command(const std::string& command, int timeout_seconds) {
    std::string full_command = "timeout " + std::to_string(timeout_seconds) + " " + command;
    return system(full_command.c_str()) == 0 ? "success" : "failed";
}

std::pair<bool, std::string> SystemManager::execute_command_with_result(const std::string& command, int timeout_seconds) {
    std::string full_command = "timeout " + std::to_string(timeout_seconds) + " " + command + " 2>&1";
    
    FILE* pipe = popen(full_command.c_str(), "r");
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

int SystemManager::get_uptime_seconds() {
    if (file_exists("/proc/uptime")) {
        std::string uptime_str = read_file("/proc/uptime");
        std::istringstream iss(uptime_str);
        double uptime;
        if (iss >> uptime) {
            return static_cast<int>(uptime);
        }
    }
    return 0;
}

double SystemManager::get_memory_usage_percent() {
    if (file_exists("/proc/meminfo")) {
        std::string meminfo = read_file("/proc/meminfo");
        std::istringstream iss(meminfo);
        std::string line;
        
        long total_mem = 0, free_mem = 0, available_mem = 0;
        
        while (std::getline(iss, line)) {
            if (line.find("MemTotal:") == 0) {
                std::istringstream line_stream(line);
                std::string key, value, unit;
                line_stream >> key >> value >> unit;
                total_mem = std::stol(value);
            } else if (line.find("MemAvailable:") == 0) {
                std::istringstream line_stream(line);
                std::string key, value, unit;
                line_stream >> key >> value >> unit;
                available_mem = std::stol(value);
            }
        }
        
        if (total_mem > 0) {
            return ((total_mem - available_mem) * 100.0) / total_mem;
        }
    }
    return 0.0;
}

double SystemManager::get_disk_usage_percent() {
    struct statvfs stat;
    if (statvfs("/", &stat) == 0) {
        unsigned long total = stat.f_blocks * stat.f_frsize;
        unsigned long available = stat.f_bavail * stat.f_frsize;
        unsigned long used = total - available;
        return (used * 100.0) / total;
    }
    return 0.0;
}

double SystemManager::get_cpu_usage_percent() {
    // Simple CPU usage calculation
    if (file_exists("/proc/loadavg")) {
        std::string loadavg = read_file("/proc/loadavg");
        std::istringstream iss(loadavg);
        double load1, load5, load15;
        if (iss >> load1 >> load5 >> load15) {
            return load1 * 100.0; // Approximate CPU usage
        }
    }
    return 0.0;
}

bool SystemManager::is_reboot_required() {
    return file_exists("/var/run/reboot-required");
}

std::string SystemManager::read_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return "";
    }
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    return content;
}

bool SystemManager::write_file(const std::string& file_path, const std::string& content) {
    std::ofstream file(file_path);
    if (!file.is_open()) {
        return false;
    }
    
    file << content;
    file.close();
    return true;
}

bool SystemManager::file_exists(const std::string& file_path) {
    struct stat buffer;
    return stat(file_path.c_str(), &buffer) == 0;
}

void SystemManager::log_event(const std::string& level, const std::string& message, const std::string& component) {
    SystemLog log(level, message, component);
    Database::instance().log_event(log);
}

} // namespace vaultusb
