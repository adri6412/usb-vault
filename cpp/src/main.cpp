// VaultUSB C++ Server
// Complete implementation with authentication, encryption, and file management

#include "config.h"
#include "database.h"
#include "auth.h"
#include "crypto.h"
#include "storage.h"
#include "wifi.h"
#include "system.h"
#include "http_server.h"

#include <iostream>
#include <signal.h>
#include <unistd.h>

namespace vaultusb {

class VaultUSBApp {
public:
    static VaultUSBApp& instance() {
        static VaultUSBApp instance;
        return instance;
    }
    
    bool initialize(int argc, char* argv[]) {
        // Parse command line arguments
        int port = 8000;
        std::string config_file = "config.toml";
        
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc) {
                port = std::atoi(argv[++i]);
            } else if (arg == "--config" && i + 1 < argc) {
                config_file = argv[++i];
            } else if (arg == "--help") {
                print_help();
                return false;
            }
        }
        
        // Load configuration
        Config::instance().load_from_file(config_file);
        
        // Initialize database
        if (!Database::instance().initialize(Config::instance().db_file())) {
            std::cerr << "Failed to initialize database" << std::endl;
            return false;
        }
        
        // Initialize crypto manager
        CryptoManager::instance();
        
        // Initialize other managers
        AuthManager::instance();
        StorageManager::instance();
        WiFiManager::instance();
        SystemManager::instance();
        
        // Initialize HTTP server
        if (!HttpServer::instance().initialize(port)) {
            std::cerr << "Failed to initialize HTTP server" << std::endl;
            return false;
        }
        
        std::cout << "VaultUSB C++ server initialized on port " << port << std::endl;
        return true;
    }
    
    void run() {
        // Set up signal handlers
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        
        // Run the HTTP server
        HttpServer::instance().run();
    }
    
    void shutdown() {
        std::cout << "Shutting down VaultUSB server..." << std::endl;
        Database::instance().cleanup();
    }
    
private:
    VaultUSBApp() = default;
    VaultUSBApp(const VaultUSBApp&) = delete;
    VaultUSBApp& operator=(const VaultUSBApp&) = delete;
    
    static void signal_handler(int signal) {
        if (signal == SIGINT || signal == SIGTERM) {
            std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
            VaultUSBApp::instance().shutdown();
            exit(0);
        }
    }
    
    void print_help() {
        std::cout << "VaultUSB C++ Server\n";
        std::cout << "Usage: vaultusb_cpp [options]\n";
        std::cout << "Options:\n";
        std::cout << "  --port PORT        Port to listen on (default: 8000)\n";
        std::cout << "  --config FILE      Configuration file (default: config.toml)\n";
        std::cout << "  --help             Show this help message\n";
    }
};

} // namespace vaultusb

int main(int argc, char* argv[]) {
    if (!vaultusb::VaultUSBApp::instance().initialize(argc, argv)) {
        return 1;
    }
    
    vaultusb::VaultUSBApp::instance().run();
    return 0;
}

