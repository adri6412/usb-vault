// Minimal HTTP server for VaultUSB in C++
// NOTE: This is a lightweight placeholder implementation mapping key endpoints.
// It is intentionally simple and dependency-free to ease Buildroot integration.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace {

std::string now_iso8601() {
    char buffer[64];
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

std::string to_lower(const std::string &s) {
    std::string o = s;
    for (char &c : o) c = static_cast<char>(::tolower(c));
    return o;
}

bool parse_request(const std::string &raw, HttpRequest &req) {
    auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) return false;

    std::istringstream ss(raw.substr(0, header_end));
    std::string line;
    if (!std::getline(ss, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::istringstream l1(line);
    if (!(l1 >> req.method >> req.path)) return false;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = to_lower(line.substr(0, colon));
        std::string value = line.substr(colon + 1);
        // trim leading space
        if (!value.empty() && value[0] == ' ') value.erase(0, 1);
        req.headers[key] = value;
    }

    // Body
    req.body = raw.substr(header_end + 4);
    auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
        // OK; body length checked by caller
    }
    return true;
}

std::string http_response(int status, const std::string &status_text, const std::string &content_type, const std::string &body) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << status_text << "\r\n";
    out << "Content-Type: " << content_type << "\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: close\r\n\r\n";
    out << body;
    return out.str();
}

std::string json_escape(const std::string &s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default: o << c; break;
        }
    }
    return o.str();
}

// Very naive JSON body extraction for {"username":"..","password":".."}
void parse_login_body(const std::string &body, std::string &username, std::string &password) {
    auto find_value = [&](const std::string &key) -> std::string {
        auto pos = body.find('"' + key + '"');
        if (pos == std::string::npos) return {};
        pos = body.find(':', pos);
        if (pos == std::string::npos) return {};
        pos = body.find('"', pos);
        if (pos == std::string::npos) return {};
        auto end = body.find('"', pos + 1);
        if (end == std::string::npos) return {};
        return body.substr(pos + 1, end - pos - 1);
    };
    username = find_value("username");
    password = find_value("password");
}

std::string handle_request(const HttpRequest &req) {
    if (req.method == "GET" && req.path == "/health") {
        std::ostringstream b;
        b << "{\"status\":\"healthy\",\"timestamp\":\"" << json_escape(now_iso8601()) << "\"}";
        return http_response(200, "OK", "application/json", b.str());
    }

    if (req.method == "POST" && req.path == "/api/auth/login") {
        std::string username, password;
        parse_login_body(req.body, username, password);
        bool success = (username == "admin" && password == "admin");
        std::ostringstream b;
        if (success) {
            b << "{\"success\":true,\"message\":\"Login successful\",\"session_id\":\"dummy-session\"}";
            return http_response(200, "OK", "application/json", b.str());
        } else {
            b << "{\"success\":false,\"message\":\"Invalid username or password\"}";
            return http_response(401, "Unauthorized", "application/json", b.str());
        }
    }

    if (req.method == "GET" && req.path == "/") {
        const std::string body = "<html><body><h1>VaultUSB (C++)</h1><p>Server up.</p></body></html>";
        return http_response(200, "OK", "text/html; charset=utf-8", body);
    }

    const std::string not_impl = "{\"error\":\"Not Implemented\"}";
    return http_response(501, "Not Implemented", "application/json", not_impl);
}

void serve(uint16_t port) {
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::perror("socket");
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        close(server_fd);
        return;
    }
    if (listen(server_fd, 16) < 0) {
        std::perror("listen");
        close(server_fd);
        return;
    }

    std::cout << "VaultUSB C++ server listening on 0.0.0.0:" << port << std::endl;

    constexpr size_t kMax = 1 << 20; // 1MB cap per request
    for (;;) {
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        int fd = accept(server_fd, reinterpret_cast<sockaddr *>(&cli), &len);
        if (fd < 0) {
            std::perror("accept");
            continue;
        }

        std::string data;
        data.resize(0);
        char buf[4096];
        ssize_t n;

        // Read until we got headers and full body (by Content-Length)
        size_t content_length = 0;
        bool have_headers = false;
        size_t header_end_pos = std::string::npos;

        while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
            data.append(buf, buf + n);
            if (!have_headers) {
                header_end_pos = data.find("\r\n\r\n");
                if (header_end_pos != std::string::npos) {
                    have_headers = true;
                    // Parse Content-Length
                    auto hdrs = data.substr(0, header_end_pos);
                    std::istringstream hs(hdrs);
                    std::string line;
                    while (std::getline(hs, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        auto colon = line.find(':');
                        if (colon != std::string::npos) {
                            std::string key = to_lower(line.substr(0, colon));
                            std::string val = line.substr(colon + 1);
                            if (!val.empty() && val[0] == ' ') val.erase(0, 1);
                            if (key == "content-length") {
                                content_length = static_cast<size_t>(std::stoul(val));
                            }
                        }
                    }
                }
            }
            if (have_headers) {
                size_t body_received = data.size() - (header_end_pos + 4);
                if (content_length == 0 || body_received >= content_length) break;
            }
            if (data.size() > kMax) break;
        }

        HttpRequest req;
        std::string resp;
        if (parse_request(data, req)) {
            resp = handle_request(req);
        } else {
            const std::string body = "{\"error\":\"Bad Request\"}";
            resp = http_response(400, "Bad Request", "application/json", body);
        }

        ssize_t off = 0;
        while (off < static_cast<ssize_t>(resp.size())) {
            ssize_t w = ::write(fd, resp.data() + off, resp.size() - off);
            if (w <= 0) break;
            off += w;
        }

        ::close(fd);
    }

    ::close(server_fd);
}

}  // namespace

int main(int argc, char **argv) {
    uint16_t port = 8000;
    if (argc >= 2) {
        int p = std::atoi(argv[1]);
        if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
    }
    serve(port);
    return 0;
}

