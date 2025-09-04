#pragma once

#include "models.h"
#include "database.h"
#include <string>
#include <vector>
#include <memory>

namespace vaultusb {

class SystemManager {
public:
    static SystemManager& instance();
    
    // System status and monitoring
    SystemStatus get_system_status();
    std::vector<PackageUpdate> check_updates();
    struct UpgradeResult {
        bool success = false;
        std::string message;
        std::string log;
    };
    UpgradeResult upgrade_system();
    std::pair<bool, std::string> reboot_system();
    
    // System information
    struct SystemInfo {
        std::string os_name;
        std::string os_version;
        std::string kernel_version;
        std::string hardware_model;
        std::string dietpi_version;
        std::string dietpi_hw_model;
    };
    SystemInfo get_system_info();
    
    // Maintenance
    void setup_sudoers();
    bool is_dietpi() const { return is_dietpi_; }
    
private:
    SystemManager() = default;
    SystemManager(const SystemManager&) = delete;
    SystemManager& operator=(const SystemManager&) = delete;
    
    bool is_dietpi_ = false;
    std::string sudoers_file_ = "/etc/sudoers.d/vaultusb";
    
    // Helper methods
    bool check_dietpi();
    std::vector<PackageUpdate> parse_upgrade_output(const std::string& output);
    std::string execute_command(const std::string& command, int timeout_seconds = 30);
    std::pair<bool, std::string> execute_command_with_result(const std::string& command, int timeout_seconds = 30);
    
    // System monitoring helpers
    int get_uptime_seconds();
    double get_memory_usage_percent();
    double get_disk_usage_percent();
    double get_cpu_usage_percent();
    bool is_reboot_required();
    
    // File operations
    std::string read_file(const std::string& file_path);
    bool write_file(const std::string& file_path, const std::string& content);
    bool file_exists(const std::string& file_path);
    
    // Logging
    void log_event(const std::string& level, const std::string& message, const std::string& component);
};

} // namespace vaultusb
