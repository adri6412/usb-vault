#include "database.h"
#include "config.h"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace vaultusb {

Database& Database::instance() {
    static Database instance;
    return instance;
}

bool Database::initialize(const std::string& db_file) {
    db_file_ = db_file;
    
    int rc = sqlite3_open(db_file.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    
    // Enable foreign keys
    execute_query("PRAGMA foreign_keys = ON");
    
    return create_tables();
}

void Database::cleanup() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::create_tables() {
    const std::vector<std::string> create_queries = {
        R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            totp_secret TEXT,
            totp_enabled BOOLEAN DEFAULT 0,
            created_at INTEGER NOT NULL,
            last_login INTEGER,
            is_active BOOLEAN DEFAULT 1
        )
        )",
        R"(
        CREATE TABLE IF NOT EXISTS files (
            id TEXT PRIMARY KEY,
            original_name TEXT NOT NULL,
            encrypted_name TEXT NOT NULL,
            size INTEGER NOT NULL,
            mime_type TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            modified_at INTEGER NOT NULL,
            user_id INTEGER NOT NULL,
            is_deleted BOOLEAN DEFAULT 0,
            FOREIGN KEY (user_id) REFERENCES users (id)
        )
        )",
        R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id TEXT PRIMARY KEY,
            user_id INTEGER NOT NULL,
            created_at INTEGER NOT NULL,
            last_activity INTEGER NOT NULL,
            is_active BOOLEAN DEFAULT 1,
            ip_address TEXT,
            user_agent TEXT,
            FOREIGN KEY (user_id) REFERENCES users (id)
        )
        )",
        R"(
        CREATE TABLE IF NOT EXISTS wifi_networks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            ssid TEXT NOT NULL,
            security TEXT NOT NULL,
            priority INTEGER DEFAULT 0,
            created_at INTEGER NOT NULL,
            is_active BOOLEAN DEFAULT 1
        )
        )",
        R"(
        CREATE TABLE IF NOT EXISTS system_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            level TEXT NOT NULL,
            message TEXT NOT NULL,
            component TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            user_id INTEGER,
            FOREIGN KEY (user_id) REFERENCES users (id)
        )
        )"
    };
    
    for (const auto& query : create_queries) {
        if (!execute_query(query)) {
            return false;
        }
    }
    
    return create_default_admin_user();
}

bool Database::create_default_admin_user() {
    // Check if admin user exists
    auto admin_user = get_user_by_username("admin");
    if (admin_user) {
        return true; // Admin user already exists
    }
    
    // Create default admin user
    User admin("admin", ""); // Password will be set by auth manager
    return create_user(admin);
}

bool Database::create_user(const User& user) {
    const std::string query = R"(
        INSERT INTO users (username, password_hash, totp_secret, totp_enabled, created_at, last_login, is_active)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    
    bind_text(stmt, 1, user.username);
    bind_text(stmt, 2, user.password_hash);
    bind_text(stmt, 3, user.totp_secret);
    bind_int(stmt, 4, user.totp_enabled ? 1 : 0);
    bind_int64(stmt, 5, user.created_at);
    bind_int64(stmt, 6, user.last_login);
    bind_int(stmt, 7, user.is_active ? 1 : 0);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

std::shared_ptr<User> Database::get_user_by_username(const std::string& username) {
    const std::string query = "SELECT * FROM users WHERE username = ? AND is_active = 1";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return nullptr;
    }
    
    bind_text(stmt, 1, username);
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto user = std::make_shared<User>();
        user->id = get_int_column(stmt, 0);
        user->username = get_text_column(stmt, 1);
        user->password_hash = get_text_column(stmt, 2);
        user->totp_secret = get_text_column(stmt, 3);
        user->totp_enabled = get_bool_column(stmt, 4);
        user->created_at = get_int64_column(stmt, 5);
        user->last_login = get_int64_column(stmt, 6);
        user->is_active = get_bool_column(stmt, 7);
        
        sqlite3_finalize(stmt);
        return user;
    }
    
    sqlite3_finalize(stmt);
    return nullptr;
}

std::shared_ptr<User> Database::get_user_by_id(int user_id) {
    const std::string query = "SELECT * FROM users WHERE id = ? AND is_active = 1";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return nullptr;
    }
    
    bind_int(stmt, 1, user_id);
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto user = std::make_shared<User>();
        user->id = get_int_column(stmt, 0);
        user->username = get_text_column(stmt, 1);
        user->password_hash = get_text_column(stmt, 2);
        user->totp_secret = get_text_column(stmt, 3);
        user->totp_enabled = get_bool_column(stmt, 4);
        user->created_at = get_int64_column(stmt, 5);
        user->last_login = get_int64_column(stmt, 6);
        user->is_active = get_bool_column(stmt, 7);
        
        sqlite3_finalize(stmt);
        return user;
    }
    
    sqlite3_finalize(stmt);
    return nullptr;
}

bool Database::update_user(const User& user) {
    const std::string query = R"(
        UPDATE users SET 
            username = ?, password_hash = ?, totp_secret = ?, totp_enabled = ?,
            last_login = ?, is_active = ?
        WHERE id = ?
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    bind_text(stmt, 1, user.username);
    bind_text(stmt, 2, user.password_hash);
    bind_text(stmt, 3, user.totp_secret);
    bind_int(stmt, 4, user.totp_enabled ? 1 : 0);
    bind_int64(stmt, 5, user.last_login);
    bind_int(stmt, 6, user.is_active ? 1 : 0);
    bind_int(stmt, 7, user.id);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool Database::create_session(const Session& session) {
    const std::string query = R"(
        INSERT INTO sessions (id, user_id, created_at, last_activity, is_active, ip_address, user_agent)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    bind_text(stmt, 1, session.id);
    bind_int(stmt, 2, session.user_id);
    bind_int64(stmt, 3, session.created_at);
    bind_int64(stmt, 4, session.last_activity);
    bind_int(stmt, 5, session.is_active ? 1 : 0);
    bind_text(stmt, 6, session.ip_address);
    bind_text(stmt, 7, session.user_agent);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

std::shared_ptr<Session> Database::get_session_by_id(const std::string& session_id) {
    const std::string query = "SELECT * FROM sessions WHERE id = ? AND is_active = 1";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return nullptr;
    }
    
    bind_text(stmt, 1, session_id);
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto session = std::make_shared<Session>();
        session->id = get_text_column(stmt, 0);
        session->user_id = get_int_column(stmt, 1);
        session->created_at = get_int64_column(stmt, 2);
        session->last_activity = get_int64_column(stmt, 3);
        session->is_active = get_bool_column(stmt, 4);
        session->ip_address = get_text_column(stmt, 5);
        session->user_agent = get_text_column(stmt, 6);
        
        sqlite3_finalize(stmt);
        return session;
    }
    
    sqlite3_finalize(stmt);
    return nullptr;
}

bool Database::update_session(const Session& session) {
    const std::string query = R"(
        UPDATE sessions SET 
            last_activity = ?, is_active = ?
        WHERE id = ?
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    bind_int64(stmt, 1, session.last_activity);
    bind_int(stmt, 2, session.is_active ? 1 : 0);
    bind_text(stmt, 3, session.id);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool Database::cleanup_expired_sessions(int timeout_seconds) {
    const std::string query = R"(
        UPDATE sessions SET is_active = 0 
        WHERE last_activity < ? AND is_active = 1
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    std::time_t cutoff = std::time(nullptr) - timeout_seconds;
    bind_int64(stmt, 1, cutoff);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool Database::create_file(const File& file) {
    const std::string query = R"(
        INSERT INTO files (id, original_name, encrypted_name, size, mime_type, created_at, modified_at, user_id, is_deleted)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    bind_text(stmt, 1, file.id);
    bind_text(stmt, 2, file.original_name);
    bind_text(stmt, 3, file.encrypted_name);
    bind_int(stmt, 4, file.size);
    bind_text(stmt, 5, file.mime_type);
    bind_int64(stmt, 6, file.created_at);
    bind_int64(stmt, 7, file.modified_at);
    bind_int(stmt, 8, file.user_id);
    bind_int(stmt, 9, file.is_deleted ? 1 : 0);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

std::shared_ptr<File> Database::get_file_by_id(const std::string& file_id) {
    const std::string query = "SELECT * FROM files WHERE id = ? AND is_deleted = 0";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return nullptr;
    }
    
    bind_text(stmt, 1, file_id);
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto file = std::make_shared<File>();
        file->id = get_text_column(stmt, 0);
        file->original_name = get_text_column(stmt, 1);
        file->encrypted_name = get_text_column(stmt, 2);
        file->size = get_int_column(stmt, 3);
        file->mime_type = get_text_column(stmt, 4);
        file->created_at = get_int64_column(stmt, 5);
        file->modified_at = get_int64_column(stmt, 6);
        file->user_id = get_int_column(stmt, 7);
        file->is_deleted = get_bool_column(stmt, 8);
        
        sqlite3_finalize(stmt);
        return file;
    }
    
    sqlite3_finalize(stmt);
    return nullptr;
}

std::vector<File> Database::get_user_files(int user_id, int limit, int offset) {
    const std::string query = R"(
        SELECT * FROM files 
        WHERE user_id = ? AND is_deleted = 0 
        ORDER BY created_at DESC 
        LIMIT ? OFFSET ?
    )";
    
    std::vector<File> files;
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return files;
    }
    
    bind_int(stmt, 1, user_id);
    bind_int(stmt, 2, limit);
    bind_int(stmt, 3, offset);
    
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        File file;
        file.id = get_text_column(stmt, 0);
        file.original_name = get_text_column(stmt, 1);
        file.encrypted_name = get_text_column(stmt, 2);
        file.size = get_int_column(stmt, 3);
        file.mime_type = get_text_column(stmt, 4);
        file.created_at = get_int64_column(stmt, 5);
        file.modified_at = get_int64_column(stmt, 6);
        file.user_id = get_int_column(stmt, 7);
        file.is_deleted = get_bool_column(stmt, 8);
        
        files.push_back(file);
    }
    
    sqlite3_finalize(stmt);
    return files;
}

bool Database::update_file(const File& file) {
    const std::string query = R"(
        UPDATE files SET 
            original_name = ?, encrypted_name = ?, size = ?, mime_type = ?,
            modified_at = ?, is_deleted = ?
        WHERE id = ?
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    bind_text(stmt, 1, file.original_name);
    bind_text(stmt, 2, file.encrypted_name);
    bind_int(stmt, 3, file.size);
    bind_text(stmt, 4, file.mime_type);
    bind_int64(stmt, 5, file.modified_at);
    bind_int(stmt, 6, file.is_deleted ? 1 : 0);
    bind_text(stmt, 7, file.id);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool Database::delete_file(const std::string& file_id) {
    const std::string query = "UPDATE files SET is_deleted = 1, modified_at = ? WHERE id = ?";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    bind_int64(stmt, 1, std::time(nullptr));
    bind_text(stmt, 2, file_id);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool Database::log_event(const SystemLog& log) {
    const std::string query = R"(
        INSERT INTO system_logs (level, message, component, created_at, user_id)
        VALUES (?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return false;
    }
    
    bind_text(stmt, 1, log.level);
    bind_text(stmt, 2, log.message);
    bind_text(stmt, 3, log.component);
    bind_int64(stmt, 4, log.created_at);
    bind_int(stmt, 5, log.user_id);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool Database::execute_query(const std::string& query) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, query.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool Database::bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
    return sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_STATIC) == SQLITE_OK;
}

bool Database::bind_int(sqlite3_stmt* stmt, int index, int value) {
    return sqlite3_bind_int(stmt, index, value) == SQLITE_OK;
}

bool Database::bind_int64(sqlite3_stmt* stmt, int index, int64_t value) {
    return sqlite3_bind_int64(stmt, index, value) == SQLITE_OK;
}

std::string Database::get_text_column(sqlite3_stmt* stmt, int column) {
    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
    return text ? std::string(text) : std::string();
}

int Database::get_int_column(sqlite3_stmt* stmt, int column) {
    return sqlite3_column_int(stmt, column);
}

int64_t Database::get_int64_column(sqlite3_stmt* stmt, int column) {
    return sqlite3_column_int64(stmt, column);
}

bool Database::get_bool_column(sqlite3_stmt* stmt, int column) {
    return sqlite3_column_int(stmt, column) != 0;
}

} // namespace vaultusb
