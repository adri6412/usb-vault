#pragma once

#include "models.h"
#include <sqlite3.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace vaultusb {

class Database {
public:
    static Database& instance();
    
    bool initialize(const std::string& db_file);
    void cleanup();
    
    // User operations
    bool create_user(const User& user);
    std::shared_ptr<User> get_user_by_username(const std::string& username);
    std::shared_ptr<User> get_user_by_id(int user_id);
    bool update_user(const User& user);
    bool delete_user(int user_id);
    
    // Session operations
    bool create_session(const Session& session);
    std::shared_ptr<Session> get_session_by_id(const std::string& session_id);
    std::vector<Session> get_user_sessions(int user_id);
    bool update_session(const Session& session);
    bool delete_session(const std::string& session_id);
    bool cleanup_expired_sessions(int timeout_seconds);
    
    // File operations
    bool create_file(const File& file);
    std::shared_ptr<File> get_file_by_id(const std::string& file_id);
    std::vector<File> get_user_files(int user_id, int limit = 100, int offset = 0);
    bool update_file(const File& file);
    bool delete_file(const std::string& file_id);
    int get_user_file_count(int user_id);
    
    // WiFi network operations
    bool create_wifi_network(const std::string& ssid, const std::string& security, int priority = 0);
    std::vector<std::string> get_saved_networks();
    bool delete_wifi_network(const std::string& ssid);
    
    // System log operations
    bool log_event(const SystemLog& log);
    std::vector<SystemLog> get_recent_logs(int limit = 100);
    
    // Database maintenance
    bool create_tables();
    bool create_default_admin_user();
    
private:
    Database() = default;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    
    sqlite3* db_ = nullptr;
    std::string db_file_;
    
    bool execute_query(const std::string& query);
    bool execute_query(const std::string& query, std::function<int(sqlite3_stmt*)> callback);
    
    // Helper methods for prepared statements
    bool bind_text(sqlite3_stmt* stmt, int index, const std::string& value);
    bool bind_int(sqlite3_stmt* stmt, int index, int value);
    bool bind_int64(sqlite3_stmt* stmt, int index, int64_t value);
    
    std::string get_text_column(sqlite3_stmt* stmt, int column);
    int get_int_column(sqlite3_stmt* stmt, int column);
    int64_t get_int64_column(sqlite3_stmt* stmt, int column);
    bool get_bool_column(sqlite3_stmt* stmt, int column);
};

} // namespace vaultusb
