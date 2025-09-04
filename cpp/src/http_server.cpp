#include "http_server.h"
#include "config.h"
#include "auth.h"
#include "storage.h"
#include "wifi.h"
#include "system.h"
#include "crypto.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <cstring>

namespace vaultusb {

HttpServer& HttpServer::instance() {
    static HttpServer instance;
    return instance;
}

bool HttpServer::initialize(int port) {
    port_ = port;
    return create_socket();
}

void HttpServer::run() {
    if (server_socket_ < 0) {
        std::cerr << "Server socket not initialized" << std::endl;
        return;
    }
    
    register_api_routes();
    running_ = true;
    
    std::cout << "VaultUSB HTTP server listening on 0.0.0.0:" << port_ << std::endl;
    accept_connections();
}

void HttpServer::stop() {
    running_ = false;
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }
}

void HttpServer::register_route(const std::string& method, const std::string& path, 
                               std::function<HttpResponse(const HttpRequest&)> handler) {
    routes_[method][path] = handler;
}

void HttpServer::add_middleware(std::function<bool(const HttpRequest&, HttpResponse&)> middleware) {
    middlewares_.push_back(middleware);
}

bool HttpServer::create_socket() {
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        std::perror("socket");
        return false;
    }
    
    int opt = 1;
    if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::perror("setsockopt");
        close(server_socket_);
        return false;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(server_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        close(server_socket_);
        return false;
    }
    
    if (listen(server_socket_, 16) < 0) {
        std::perror("listen");
        close(server_socket_);
        return false;
    }
    
    return true;
}

void HttpServer::accept_connections() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_socket < 0) {
            if (running_) {
                std::perror("accept");
            }
            continue;
        }
        
        // Handle connection in current thread (for simplicity)
        // In production, use thread pool or async I/O
        handle_connection(client_socket);
        close(client_socket);
    }
}

void HttpServer::handle_connection(int client_socket) {
    constexpr size_t max_request_size = 1024 * 1024; // 1MB
    std::string request_data;
    char buffer[4096];
    
    // Read request
    ssize_t bytes_read;
    while ((bytes_read = read(client_socket, buffer, sizeof(buffer))) > 0) {
        request_data.append(buffer, bytes_read);
        
        // Check if we have complete headers
        if (request_data.find("\r\n\r\n") != std::string::npos) {
            // Parse Content-Length if present
            size_t header_end = request_data.find("\r\n\r\n");
            std::string headers = request_data.substr(0, header_end);
            
            size_t content_length = 0;
            std::istringstream header_stream(headers);
            std::string line;
            while (std::getline(header_stream, line)) {
                if (line.find("Content-Length:") == 0) {
                    std::string length_str = line.substr(15);
                    length_str.erase(0, length_str.find_first_not_of(" \t"));
                    try {
                        content_length = std::stoul(length_str);
                    } catch (const std::exception&) {
                        // Ignore parsing errors
                    }
                    break;
                }
            }
            
            // Check if we have complete body
            size_t body_start = header_end + 4;
            if (request_data.length() >= body_start + content_length) {
                break;
            }
        }
        
        if (request_data.length() > max_request_size) {
            break;
        }
    }
    
    if (bytes_read < 0) {
        std::perror("read");
        return;
    }
    
    // Parse request
    HttpRequest request = parse_request(request_data);
    
    // Apply middlewares
    HttpResponse response;
    for (const auto& middleware : middlewares_) {
        if (!middleware(request, response)) {
            send_response(client_socket, response);
            return;
        }
    }
    
    // Find and execute handler
    auto handler = find_route(request.method, request.path);
    if (handler) {
        response = handler(request);
    } else {
        response = HttpResponse(404, "Not Found");
        response.body = "{\"error\":\"Not Found\"}";
    }
    
    send_response(client_socket, response);
}

HttpRequest HttpServer::parse_request(const std::string& raw_request) {
    HttpRequest request;
    
    size_t header_end = raw_request.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return request;
    }
    
    std::string headers = raw_request.substr(0, header_end);
    request.body = raw_request.substr(header_end + 4);
    
    std::istringstream header_stream(headers);
    std::string line;
    
    // Parse request line
    if (std::getline(header_stream, line)) {
        std::istringstream request_line(line);
        request_line >> request.method >> request.path;
        
        // Parse query parameters
        size_t query_pos = request.path.find('?');
        if (query_pos != std::string::npos) {
            std::string query_string = request.path.substr(query_pos + 1);
            request.path = request.path.substr(0, query_pos);
            request.query_params = parse_query_string(query_string);
        }
    }
    
    // Parse headers
    while (std::getline(header_stream, line)) {
        if (line.empty()) break;
        
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);
            
            // Convert to lowercase
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            request.headers[key] = value;
        }
    }
    
    return request;
}

std::string HttpServer::build_response(const HttpResponse& response) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << response.status_code << " " << response.status_text << "\r\n";
    oss << "Content-Type: " << response.content_type << "\r\n";
    oss << "Content-Length: " << response.body.length() << "\r\n";
    oss << "Connection: close\r\n";
    
    for (const auto& header : response.headers) {
        oss << header.first << ": " << header.second << "\r\n";
    }
    
    oss << "\r\n" << response.body;
    return oss.str();
}

void HttpServer::send_response(int client_socket, const HttpResponse& response) {
    std::string response_str = build_response(response);
    ssize_t bytes_sent = 0;
    
    while (bytes_sent < static_cast<ssize_t>(response_str.length())) {
        ssize_t result = write(client_socket, response_str.c_str() + bytes_sent, 
                              response_str.length() - bytes_sent);
        if (result < 0) {
            std::perror("write");
            break;
        }
        bytes_sent += result;
    }
}

std::function<HttpResponse(const HttpRequest&)> HttpServer::find_route(const std::string& method, const std::string& path) {
    auto method_routes = routes_.find(method);
    if (method_routes == routes_.end()) {
        return nullptr;
    }
    
    // Try exact match first
    auto exact_match = method_routes->second.find(path);
    if (exact_match != method_routes->second.end()) {
        return exact_match->second;
    }
    
    // Try pattern matching
    for (const auto& route : method_routes->second) {
        if (match_route(route.first, path)) {
            return route.second;
        }
    }
    
    return nullptr;
}

bool HttpServer::match_route(const std::string& pattern, const std::string& path) {
    // Simple pattern matching - in production, use regex
    if (pattern.find("{") != std::string::npos) {
        // Handle parameterized routes like /api/files/{file_id}
        size_t pattern_pos = 0;
        size_t path_pos = 0;
        
        while (pattern_pos < pattern.length() && path_pos < path.length()) {
            if (pattern[pattern_pos] == '{') {
                // Skip parameter
                size_t param_end = pattern.find('}', pattern_pos);
                if (param_end == std::string::npos) return false;
                pattern_pos = param_end + 1;
                
                // Find next separator in path
                while (path_pos < path.length() && path[path_pos] != '/') {
                    path_pos++;
                }
            } else if (pattern[pattern_pos] == path[path_pos]) {
                pattern_pos++;
                path_pos++;
            } else {
                return false;
            }
        }
        
        return pattern_pos == pattern.length() && path_pos == path.length();
    }
    
    return pattern == path;
}

bool HttpServer::auth_middleware(const HttpRequest& request, HttpResponse& response) {
    // Skip auth for certain paths
    if (request.path == "/" || request.path == "/health" || 
        request.path.find("/static/") == 0 || request.path.find("/api/auth/login") == 0) {
        return true;
    }
    
    // Check for Authorization header
    auto auth_it = request.headers.find("authorization");
    if (auth_it == request.headers.end()) {
        response = HttpResponse(401, "Unauthorized");
        response.body = "{\"error\":\"Missing authorization header\"}";
        return false;
    }
    
    // Extract token
    std::string auth_header = auth_it->second;
    if (auth_header.find("Bearer ") != 0) {
        response = HttpResponse(401, "Unauthorized");
        response.body = "{\"error\":\"Invalid authorization format\"}";
        return false;
    }
    
    std::string token = auth_header.substr(7);
    auto user = AuthManager::instance().verify_session(token);
    
    if (!user) {
        response = HttpResponse(401, "Unauthorized");
        response.body = "{\"error\":\"Invalid or expired token\"}";
        return false;
    }
    
    return true;
}

std::shared_ptr<User> HttpServer::get_current_user(const HttpRequest& request) {
    auto auth_it = request.headers.find("authorization");
    if (auth_it == request.headers.end()) {
        return nullptr;
    }
    
    std::string token = auth_it->second.substr(7); // Remove "Bearer "
    return AuthManager::instance().verify_session(token);
}

bool HttpServer::check_vault_unlocked() {
    return vault_unlocked_ && CryptoManager::instance().is_unlocked();
}

void HttpServer::update_activity() {
    last_activity_ = std::time(nullptr);
}

void HttpServer::register_api_routes() {
    // Add auth middleware
    add_middleware([this](const HttpRequest& req, HttpResponse& resp) {
        return auth_middleware(req, resp);
    });
    
    // Register routes
    register_route("GET", "/", [this](const HttpRequest& req) { return handle_root(req); });
    register_route("GET", "/health", [this](const HttpRequest& req) { return handle_health_check(req); });
    register_route("POST", "/api/auth/login", [this](const HttpRequest& req) { return handle_login(req); });
    register_route("POST", "/api/auth/logout", [this](const HttpRequest& req) { return handle_logout(req); });
    register_route("POST", "/api/vault/unlock", [this](const HttpRequest& req) { return handle_unlock_vault(req); });
    register_route("POST", "/api/vault/lock", [this](const HttpRequest& req) { return handle_lock_vault(req); });
    register_route("GET", "/api/vault/status", [this](const HttpRequest& req) { return handle_vault_status(req); });
    register_route("GET", "/api/files", [this](const HttpRequest& req) { return handle_list_files(req); });
    register_route("POST", "/api/files/upload", [this](const HttpRequest& req) { return handle_upload_file(req); });
    register_route("GET", "/api/wifi/networks", [this](const HttpRequest& req) { return handle_scan_wifi(req); });
    register_route("GET", "/api/wifi/status", [this](const HttpRequest& req) { return handle_wifi_status(req); });
    register_route("GET", "/api/system/status", [this](const HttpRequest& req) { return handle_system_status(req); });
}

HttpResponse HttpServer::handle_root(const HttpRequest& request) {
    HttpResponse response(200, "OK");
    response.content_type = "text/html; charset=utf-8";
    response.body = R"(<!DOCTYPE html>
<html>
<head>
    <title>VaultUSB</title>
    <meta charset="utf-8">
</head>
<body>
    <h1>VaultUSB C++ Server</h1>
    <p>Server is running. Please use the web interface to manage your vault.</p>
    <p><a href="/dashboard">Go to Dashboard</a></p>
</body>
</html>)";
    return response;
}

HttpResponse HttpServer::handle_health_check(const HttpRequest& request) {
    HttpResponse response(200, "OK");
    response.body = "{\"status\":\"healthy\",\"timestamp\":\"" + now_iso8601() + "\"}";
    return response;
}

HttpResponse HttpServer::handle_login(const HttpRequest& request) {
    // Simple JSON parsing for login
    std::string username, password;
    
    // Extract username and password from JSON body
    size_t user_pos = request.body.find("\"username\":");
    if (user_pos != std::string::npos) {
        size_t user_start = request.body.find("\"", user_pos + 11) + 1;
        size_t user_end = request.body.find("\"", user_start);
        username = request.body.substr(user_start, user_end - user_start);
    }
    
    size_t pass_pos = request.body.find("\"password\":");
    if (pass_pos != std::string::npos) {
        size_t pass_start = request.body.find("\"", pass_pos + 11) + 1;
        size_t pass_end = request.body.find("\"", pass_start);
        password = request.body.substr(pass_start, pass_end - pass_start);
    }
    
    auto user = AuthManager::instance().authenticate_user(username, password);
    if (user) {
        std::string token = AuthManager::instance().create_session(*user, request.client_ip, request.user_agent);
        
        HttpResponse response(200, "OK");
        response.body = "{\"success\":true,\"message\":\"Login successful\",\"session_id\":\"" + token + "\"}";
        return response;
    } else {
        HttpResponse response(401, "Unauthorized");
        response.body = "{\"success\":false,\"message\":\"Invalid username or password\"}";
        return response;
    }
}

HttpResponse HttpServer::handle_logout(const HttpRequest& request) {
    HttpResponse response(200, "OK");
    response.body = "{\"success\":true,\"message\":\"Logged out successfully\"}";
    return response;
}

HttpResponse HttpServer::handle_unlock_vault(const HttpRequest& request) {
    // Extract password from form data
    std::string password;
    size_t pass_pos = request.body.find("password=");
    if (pass_pos != std::string::npos) {
        password = request.body.substr(pass_pos + 9);
        // URL decode
        password = url_decode(password);
    }
    
    if (CryptoManager::instance().load_master_key(password)) {
        vault_unlocked_ = true;
        update_activity();
        
        HttpResponse response(200, "OK");
        response.body = "{\"success\":true,\"message\":\"Vault unlocked successfully\"}";
        return response;
    } else {
        HttpResponse response(200, "OK");
        response.body = "{\"success\":false,\"message\":\"Invalid master password\"}";
        return response;
    }
}

HttpResponse HttpServer::handle_lock_vault(const HttpRequest& request) {
    CryptoManager::instance().lock();
    vault_unlocked_ = false;
    
    HttpResponse response(200, "OK");
    response.body = "{\"success\":true,\"message\":\"Vault locked successfully\"}";
    return response;
}

HttpResponse HttpServer::handle_vault_status(const HttpRequest& request) {
    HttpResponse response(200, "OK");
    response.body = "{\"unlocked\":" + std::string(check_vault_unlocked() ? "true" : "false") + 
                   ",\"last_activity\":" + std::to_string(last_activity_) + "}";
    return response;
}

HttpResponse HttpServer::handle_list_files(const HttpRequest& request) {
    if (!check_vault_unlocked()) {
        HttpResponse response(423, "Locked");
        response.body = "{\"error\":\"Vault is locked\"}";
        return response;
    }
    
    auto user = get_current_user(request);
    if (!user) {
        HttpResponse response(401, "Unauthorized");
        response.body = "{\"error\":\"Invalid user\"}";
        return response;
    }
    
    update_activity();
    auto files = StorageManager::instance().list_files(*user);
    
    std::ostringstream json;
    json << "{\"files\":[";
    for (size_t i = 0; i < files.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"id\":\"" << files[i].id << "\","
             << "\"original_name\":\"" << json_escape(files[i].original_name) << "\","
             << "\"size\":" << files[i].size << ","
             << "\"mime_type\":\"" << json_escape(files[i].mime_type) << "\","
             << "\"created_at\":" << files[i].created_at << ","
             << "\"modified_at\":" << files[i].modified_at << "}";
    }
    json << "],\"total\":" << files.size() << "}";
    
    HttpResponse response(200, "OK");
    response.body = json.str();
    return response;
}

HttpResponse HttpServer::handle_upload_file(const HttpRequest& request) {
    // Simplified file upload - in production, use proper multipart parsing
    HttpResponse response(501, "Not Implemented");
    response.body = "{\"error\":\"File upload not implemented in this version\"}";
    return response;
}

HttpResponse HttpServer::handle_scan_wifi(const HttpRequest& request) {
    update_activity();
    auto networks = WiFiManager::instance().scan_networks();
    
    std::ostringstream json;
    json << "[";
    for (size_t i = 0; i < networks.size(); i++) {
        if (i > 0) json << ",";
        json << "{\"ssid\":\"" << json_escape(networks[i].ssid) << "\","
             << "\"bssid\":\"" << json_escape(networks[i].bssid) << "\","
             << "\"frequency\":" << networks[i].frequency << ","
             << "\"signal_level\":" << networks[i].signal_level << ","
             << "\"security\":\"" << json_escape(networks[i].security) << "\"}";
    }
    json << "]";
    
    HttpResponse response(200, "OK");
    response.body = json.str();
    return response;
}

HttpResponse HttpServer::handle_wifi_status(const HttpRequest& request) {
    update_activity();
    auto status = WiFiManager::instance().get_status();
    
    HttpResponse response(200, "OK");
    response.body = "{\"interface\":\"" + json_escape(status.interface) + 
                   "\",\"status\":\"" + json_escape(status.status) + 
                   "\",\"ssid\":\"" + json_escape(status.ssid) + 
                   "\",\"ip_address\":\"" + json_escape(status.ip_address) + 
                   "\",\"signal_level\":" + std::to_string(status.signal_level) + "}";
    return response;
}

HttpResponse HttpServer::handle_system_status(const HttpRequest& request) {
    update_activity();
    auto status = SystemManager::instance().get_system_status();
    
    HttpResponse response(200, "OK");
    response.body = "{\"uptime\":" + std::to_string(status.uptime) + 
                   ",\"memory_usage\":" + std::to_string(status.memory_usage) + 
                   ",\"disk_usage\":" + std::to_string(status.disk_usage) + 
                   ",\"cpu_usage\":" + std::to_string(status.cpu_usage) + 
                   ",\"reboot_required\":" + std::string(status.reboot_required ? "true" : "false") + "}";
    return response;
}

std::string HttpServer::url_decode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '%' && i + 2 < str.length()) {
            std::string hex = str.substr(i + 1, 2);
            char c = static_cast<char>(std::stoi(hex, nullptr, 16));
            result += c;
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

std::string HttpServer::url_encode(const std::string& str) {
    std::ostringstream escaped;
    for (char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::hex << std::uppercase << static_cast<int>(c);
        }
    }
    return escaped.str();
}

std::map<std::string, std::string> HttpServer::parse_query_string(const std::string& query) {
    std::map<std::string, std::string> params;
    std::istringstream iss(query);
    std::string pair;
    
    while (std::getline(iss, pair, '&')) {
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + 1);
            params[url_decode(key)] = url_decode(value);
        }
    }
    
    return params;
}

std::string HttpServer::json_escape(const std::string& str) {
    std::ostringstream escaped;
    for (char c : str) {
        switch (c) {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default: escaped << c; break;
        }
    }
    return escaped.str();
}

std::string HttpServer::now_iso8601() {
    char buffer[64];
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

} // namespace vaultusb
