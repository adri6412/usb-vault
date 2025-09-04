#pragma once

#include "models.h"
#include "auth.h"
#include "storage.h"
#include "wifi.h"
#include "system.h"
#include "crypto.h"
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <functional>

namespace vaultusb {

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
    std::map<std::string, std::string> query_params;
    std::string client_ip;
    std::string user_agent;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::map<std::string, std::string> headers;
    std::string body;
    std::string content_type = "application/json";
    
    HttpResponse() = default;
    HttpResponse(int code, const std::string& text) : status_code(code), status_text(text) {}
};

class HttpServer {
public:
    static HttpServer& instance();
    
    bool initialize(int port = 8000);
    void run();
    void stop();
    
    // Route registration
    void register_route(const std::string& method, const std::string& path, 
                       std::function<HttpResponse(const HttpRequest&)> handler);
    
    // Middleware
    void add_middleware(std::function<bool(const HttpRequest&, HttpResponse&)> middleware);
    
    // Utility methods
    static std::string url_decode(const std::string& str);
    static std::string url_encode(const std::string& str);
    static std::map<std::string, std::string> parse_query_string(const std::string& query);
    static std::string json_escape(const std::string& str);
    static std::string now_iso8601();
    
private:
    HttpServer() = default;
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    
    int port_ = 8000;
    int server_socket_ = -1;
    bool running_ = false;
    
    std::map<std::string, std::map<std::string, std::function<HttpResponse(const HttpRequest&)>>> routes_;
    std::vector<std::function<bool(const HttpRequest&, HttpResponse&)>> middlewares_;
    
    // Server operations
    bool create_socket();
    void accept_connections();
    void handle_connection(int client_socket);
    HttpRequest parse_request(const std::string& raw_request);
    std::string build_response(const HttpResponse& response);
    void send_response(int client_socket, const HttpResponse& response);
    
    // Route matching
    std::function<HttpResponse(const HttpRequest&)> find_route(const std::string& method, const std::string& path);
    bool match_route(const std::string& pattern, const std::string& path);
    
    // Authentication middleware
    bool auth_middleware(const HttpRequest& request, HttpResponse& response);
    std::shared_ptr<User> get_current_user(const HttpRequest& request);
    
    // Vault state management
    bool vault_unlocked_ = false;
    std::time_t last_activity_ = 0;
    bool check_vault_unlocked();
    void update_activity();
    
    // API handlers
    void register_api_routes();
    HttpResponse handle_login(const HttpRequest& request);
    HttpResponse handle_logout(const HttpRequest& request);
    HttpResponse handle_change_password(const HttpRequest& request);
    HttpResponse handle_setup_totp(const HttpRequest& request);
    HttpResponse handle_verify_totp(const HttpRequest& request);
    HttpResponse handle_unlock_vault(const HttpRequest& request);
    HttpResponse handle_lock_vault(const HttpRequest& request);
    HttpResponse handle_vault_status(const HttpRequest& request);
    HttpResponse handle_list_files(const HttpRequest& request);
    HttpResponse handle_upload_file(const HttpRequest& request);
    HttpResponse handle_download_file(const HttpRequest& request);
    HttpResponse handle_preview_file(const HttpRequest& request);
    HttpResponse handle_delete_file(const HttpRequest& request);
    HttpResponse handle_scan_wifi(const HttpRequest& request);
    HttpResponse handle_wifi_status(const HttpRequest& request);
    HttpResponse handle_connect_wifi(const HttpRequest& request);
    HttpResponse handle_disconnect_wifi(const HttpRequest& request);
    HttpResponse handle_forget_wifi(const HttpRequest& request);
    HttpResponse handle_system_status(const HttpRequest& request);
    HttpResponse handle_check_updates(const HttpRequest& request);
    HttpResponse handle_upgrade_system(const HttpRequest& request);
    HttpResponse handle_reboot_system(const HttpRequest& request);
    HttpResponse handle_health_check(const HttpRequest& request);
    
    // Web UI handlers
    HttpResponse handle_root(const HttpRequest& request);
    HttpResponse handle_dashboard(const HttpRequest& request);
    HttpResponse handle_files_page(const HttpRequest& request);
    HttpResponse handle_wifi_page(const HttpRequest& request);
    HttpResponse handle_system_page(const HttpRequest& request);
    
    // Static file serving
    HttpResponse serve_static_file(const std::string& path);
    std::string get_mime_type(const std::string& filename);
    
    // Template rendering
    std::string render_template(const std::string& template_name, const std::map<std::string, std::string>& context);
    std::string load_template(const std::string& template_name);
    std::string process_template(const std::string& template_content, const std::map<std::string, std::string>& context);
};

} // namespace vaultusb
