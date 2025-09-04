#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <sqlite3.h>
#include <jansson.h>

#define PORT 8000
#define MAX_CLIENTS 10
#define BUFFER_SIZE 4096
#define MAX_PATH_LEN 256
#define MAX_USERNAME_LEN 64
#define MAX_PASSWORD_LEN 128
#define SALT_LEN 32
#define HASH_LEN 64

typedef struct {
    int socket;
    char username[MAX_USERNAME_LEN];
    int authenticated;
} client_t;

typedef struct {
    char username[MAX_USERNAME_LEN];
    char password_hash[HASH_LEN];
    char salt[SALT_LEN];
    int admin;
} user_t;

static int server_socket;
static client_t clients[MAX_CLIENTS];
static sqlite3 *db;
static int running = 1;

// Database functions
int init_database() {
    int rc = sqlite3_open("/opt/vaultusb/vault.db", &db);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    char *sql = "CREATE TABLE IF NOT EXISTS users ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "username TEXT UNIQUE NOT NULL,"
                "password_hash TEXT NOT NULL,"
                "salt TEXT NOT NULL,"
                "admin INTEGER DEFAULT 0,"
                "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                ");";
    
    rc = sqlite3_exec(db, sql, 0, 0, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    
    return 0;
}

// Authentication functions
void generate_salt(char *salt) {
    if (RAND_bytes((unsigned char*)salt, SALT_LEN) != 1) {
        // Fallback to time-based salt
        srand(time(NULL));
        for (int i = 0; i < SALT_LEN; i++) {
            salt[i] = rand() % 256;
        }
    }
}

void hash_password(const char *password, const char *salt, char *hash) {
    unsigned char digest[SHA512_DIGEST_LENGTH];
    char combined[MAX_PASSWORD_LEN + SALT_LEN];
    
    snprintf(combined, sizeof(combined), "%s%s", password, salt);
    SHA512((unsigned char*)combined, strlen(combined), digest);
    
    for (int i = 0; i < SHA512_DIGEST_LENGTH; i++) {
        sprintf(hash + (i * 2), "%02x", digest[i]);
    }
}

int authenticate_user(const char *username, const char *password) {
    char *sql = "SELECT password_hash, salt FROM users WHERE username = ?";
    sqlite3_stmt *stmt;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *stored_hash = (const char*)sqlite3_column_text(stmt, 0);
        const char *stored_salt = (const char*)sqlite3_column_text(stmt, 1);
        
        char computed_hash[HASH_LEN];
        hash_password(password, stored_salt, computed_hash);
        
        sqlite3_finalize(stmt);
        return strcmp(stored_hash, computed_hash) == 0;
    }
    
    sqlite3_finalize(stmt);
    return 0;
}

// HTTP response functions
void send_http_response(int client_socket, int status_code, const char *content_type, const char *body) {
    char response[BUFFER_SIZE];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n%s",
        status_code,
        status_code == 200 ? "OK" : "Unauthorized",
        content_type,
        strlen(body),
        body
    );
    
    send(client_socket, response, len, 0);
}

void send_login_page(int client_socket) {
    const char *html = 
        "<!DOCTYPE html>"
        "<html><head><title>VaultUSB Login</title></head>"
        "<body>"
        "<h1>VaultUSB Login</h1>"
        "<form method='post' action='/login'>"
        "<input type='text' name='username' placeholder='Username' required><br><br>"
        "<input type='password' name='password' placeholder='Password' required><br><br>"
        "<button type='submit'>Login</button>"
        "</form>"
        "</body></html>";
    
    send_http_response(client_socket, 200, "text/html", html);
}

void send_dashboard(int client_socket) {
    const char *html = 
        "<!DOCTYPE html>"
        "<html><head><title>VaultUSB Dashboard</title></head>"
        "<body>"
        "<h1>VaultUSB Dashboard</h1>"
        "<h2>Welcome to your secure vault!</h2>"
        "<p>Your files are safely stored and encrypted.</p>"
        "<a href='/logout'>Logout</a>"
        "</body></html>";
    
    send_http_response(client_socket, 200, "text/html", html);
}

// Request parsing
int parse_http_request(const char *request, char *method, char *path, char *body) {
    if (sscanf(request, "%s %s HTTP/1.1", method, path) != 2) {
        return -1;
    }
    
    // Find body after double CRLF
    const char *body_start = strstr(request, "\r\n\r\n");
    if (body_start) {
        strcpy(body, body_start + 4);
    } else {
        body[0] = '\0';
    }
    
    return 0;
}

// Main request handler
void handle_request(int client_socket) {
    char buffer[BUFFER_SIZE];
    char method[16], path[256], body[BUFFER_SIZE];
    
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        return;
    }
    
    buffer[bytes_received] = '\0';
    
    if (parse_http_request(buffer, method, path, body) != 0) {
        send_http_response(client_socket, 400, "text/plain", "Bad Request");
        return;
    }
    
    printf("Request: %s %s\n", method, path);
    
    if (strcmp(path, "/") == 0) {
        send_login_page(client_socket);
    } else if (strcmp(path, "/login") == 0 && strcmp(method, "POST") == 0) {
        // Parse form data
        char username[MAX_USERNAME_LEN] = {0};
        char password[MAX_PASSWORD_LEN] = {0};
        
        // Simple form parsing
        char *user_start = strstr(body, "username=");
        char *pass_start = strstr(body, "password=");
        
        if (user_start && pass_start) {
            sscanf(user_start, "username=%63[^&]", username);
            sscanf(pass_start, "password=%127[^&]", password);
            
            if (authenticate_user(username, password)) {
                send_dashboard(client_socket);
            } else {
                send_http_response(client_socket, 401, "text/plain", "Invalid credentials");
            }
        } else {
            send_http_response(client_socket, 400, "text/plain", "Missing credentials");
        }
    } else if (strcmp(path, "/dashboard") == 0) {
        send_dashboard(client_socket);
    } else {
        send_http_response(client_socket, 404, "text/plain", "Not Found");
    }
}

// Signal handler
void signal_handler(int sig) {
    printf("\nShutting down server...\n");
    running = 0;
    close(server_socket);
    sqlite3_close(db);
    exit(0);
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Initialize database
    if (init_database() != 0) {
        fprintf(stderr, "Failed to initialize database\n");
        return 1;
    }
    
    // Create server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind socket
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    // Listen for connections
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("listen");
        return 1;
    }
    
    printf("VaultUSB server listening on port %d\n", PORT);
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Main server loop
    while (running) {
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (running) {
                perror("accept");
            }
            continue;
        }
        
        printf("Client connected: %s\n", inet_ntoa(client_addr.sin_addr));
        
        // Handle request in child process
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            close(server_socket);
            handle_request(client_socket);
            close(client_socket);
            exit(0);
        } else if (pid > 0) {
            // Parent process
            close(client_socket);
            // Clean up zombie processes
            while (waitpid(-1, NULL, WNOHANG) > 0);
        } else {
            perror("fork");
            close(client_socket);
        }
    }
    
    close(server_socket);
    sqlite3_close(db);
    return 0;
}
