#pragma once

#include <string>
#include <map>
#include <memory>

namespace vaultusb {

class Config {
public:
    static Config& instance();
    
    // App configuration
    const std::string& app_name() const { return app_name_; }
    const std::string& app_version() const { return app_version_; }
    bool debug() const { return debug_; }
    const std::string& host() const { return host_; }
    int port() const { return port_; }
    const std::string& secret_key() const { return secret_key_; }
    
    // Networking configuration
    const std::string& usb0_ip() const { return usb0_ip_; }
    const std::string& usb0_netmask() const { return usb0_netmask_; }
    const std::string& usb0_dhcp_range() const { return usb0_dhcp_range_; }
    const std::string& uap0_ip() const { return uap0_ip_; }
    const std::string& uap0_netmask() const { return uap0_netmask_; }
    const std::string& uap0_dhcp_range() const { return uap0_dhcp_range_; }
    const std::string& ap_ssid() const { return ap_ssid_; }
    const std::string& ap_password() const { return ap_password_; }
    
    // Security configuration
    int idle_timeout() const { return idle_timeout_; }
    const std::string& master_key_file() const { return master_key_file_; }
    const std::string& vault_dir() const { return vault_dir_; }
    const std::string& db_file() const { return db_file_; }
    int argon2_time_cost() const { return argon2_time_cost_; }
    int argon2_memory_cost() const { return argon2_memory_cost_; }
    int argon2_parallelism() const { return argon2_parallelism_; }
    int file_key_size() const { return file_key_size_; }
    
    // TLS configuration
    bool tls_enabled() const { return tls_enabled_; }
    const std::string& cert_file() const { return cert_file_; }
    const std::string& key_file() const { return key_file_; }
    
    // System configuration
    const std::string& sudoers_file() const { return sudoers_file_; }
    bool rpi_update_enabled() const { return rpi_update_enabled_; }
    bool dietpi_optimized() const { return dietpi_optimized_; }
    const std::string& dietpi_version() const { return dietpi_version_; }
    const std::string& python_version() const { return python_version_; }
    const std::string& debian_version() const { return debian_version_; }
    
    void load_from_file(const std::string& config_file = "config.toml");
    void set_defaults();

private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    void parse_toml_value(const std::string& key, const std::string& value);
    std::string get_value(const std::string& key, const std::string& default_value = "") const;
    int get_int_value(const std::string& key, int default_value = 0) const;
    bool get_bool_value(const std::string& key, bool default_value = false) const;
    
    std::map<std::string, std::string> config_map_;
    
    // App configuration
    std::string app_name_ = "VaultUSB";
    std::string app_version_ = "1.0.0";
    bool debug_ = false;
    std::string host_ = "0.0.0.0";
    int port_ = 8000;
    std::string secret_key_ = "vaultusb-secret-key";
    
    // Networking configuration
    std::string usb0_ip_ = "192.168.3.1";
    std::string usb0_netmask_ = "24";
    std::string usb0_dhcp_range_ = "192.168.3.100,192.168.3.200";
    std::string uap0_ip_ = "10.42.0.1";
    std::string uap0_netmask_ = "24";
    std::string uap0_dhcp_range_ = "10.42.0.100,10.42.0.200";
    std::string ap_ssid_ = "VaultUSB";
    std::string ap_password_ = "ChangeMeVault!";
    
    // Security configuration
    int idle_timeout_ = 600;
    std::string master_key_file_ = "/opt/vaultusb/master.key";
    std::string vault_dir_ = "/opt/vaultusb/vault";
    std::string db_file_ = "/opt/vaultusb/vault.db";
    int argon2_time_cost_ = 3;
    int argon2_memory_cost_ = 65536;
    int argon2_parallelism_ = 1;
    int file_key_size_ = 32;
    
    // TLS configuration
    bool tls_enabled_ = false;
    std::string cert_file_ = "/opt/vaultusb/cert.pem";
    std::string key_file_ = "/opt/vaultusb/key.pem";
    
    // System configuration
    std::string sudoers_file_ = "/etc/sudoers.d/vaultusb";
    bool rpi_update_enabled_ = true;
    bool dietpi_optimized_ = false;
    std::string dietpi_version_ = "unknown";
    std::string python_version_ = "3.11";
    std::string debian_version_ = "bookworm";
};

} // namespace vaultusb
