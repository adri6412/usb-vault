# VaultUSB C++ Implementation

This is the complete C++ implementation of VaultUSB, a secure file storage system for Raspberry Pi Zero with WiFi management capabilities.

## Features

- **Secure File Storage**: Encrypted file storage using ChaCha20-Poly1305 encryption
- **User Authentication**: Argon2id password hashing and session management
- **WiFi Management**: Scan, connect, and manage WiFi networks
- **System Monitoring**: CPU, memory, disk usage monitoring
- **Web Interface**: HTTP server with REST API
- **Buildroot Integration**: Cross-compilation for embedded systems

## Dependencies

### Build Dependencies
- CMake 3.13+
- C++17 compatible compiler
- SQLite3 development libraries
- OpenSSL development libraries
- Argon2 development libraries
- pkg-config

### Runtime Dependencies
- SQLite3
- OpenSSL
- libargon2
- Standard C++ runtime

## Building

### Local Build
```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install cmake build-essential libsqlite3-dev libssl-dev libargon2-dev pkg-config

# Build
./build_cpp.sh

# Run
./build/vaultusb_cpp --port 8000 --config config.toml
```

### Cross-compilation with Buildroot
```bash
# Setup Buildroot environment
./setup_buildroot.sh

# This will:
# 1. Download and configure Buildroot
# 2. Create a custom package for VaultUSB C++
# 3. Build a complete Raspberry Pi Zero image
# 4. Generate SD card image
```

## Configuration

The application uses TOML configuration files:
- `config.toml` - Main configuration
- `config_raspbian.toml` - Raspberry Pi OS specific
- `config_dietpi.toml` - DietPi specific
- `config_bookworm.toml` - Debian Bookworm specific

## API Endpoints

### Authentication
- `POST /api/auth/login` - User login
- `POST /api/auth/logout` - User logout
- `POST /api/auth/change-password` - Change password

### Vault Management
- `POST /api/vault/unlock` - Unlock vault with master password
- `POST /api/vault/lock` - Lock vault
- `GET /api/vault/status` - Get vault status

### File Management
- `GET /api/files` - List files
- `POST /api/files/upload` - Upload file
- `GET /api/files/{id}/download` - Download file
- `DELETE /api/files/{id}` - Delete file

### WiFi Management
- `GET /api/wifi/networks` - Scan for networks
- `GET /api/wifi/status` - Get connection status
- `POST /api/wifi/connect` - Connect to network
- `POST /api/wifi/disconnect` - Disconnect

### System Management
- `GET /api/system/status` - System status
- `GET /api/system/updates` - Check for updates
- `POST /api/system/upgrade` - Upgrade system
- `POST /api/system/reboot` - Reboot system

## Security Features

- **Argon2id Password Hashing**: Memory-hard password hashing
- **ChaCha20-Poly1305 Encryption**: Authenticated encryption for files
- **HKDF Key Derivation**: Secure key derivation for file encryption
- **Session Management**: JWT-like token-based authentication
- **Secure File Deletion**: Multiple-pass secure deletion

## Architecture

### Core Components
- **Config**: Configuration management
- **Database**: SQLite database operations
- **Crypto**: Encryption/decryption operations
- **Auth**: User authentication and session management
- **Storage**: File storage and management
- **WiFi**: WiFi network management
- **System**: System monitoring and updates
- **HttpServer**: HTTP server and API endpoints

### Data Flow
1. User authenticates via `/api/auth/login`
2. User unlocks vault via `/api/vault/unlock`
3. Files are encrypted and stored securely
4. All operations require valid session token
5. Vault can be locked to secure data

## Buildroot Integration

The C++ version is fully integrated with Buildroot for embedded deployment:

1. **Custom Package**: `board/vaultusb/package/vaultusb-cpp/`
2. **CMake Build**: Uses CMake for cross-compilation
3. **Dependencies**: Automatically handles SQLite3, OpenSSL, Argon2
4. **Init Scripts**: BusyBox init scripts for service management
5. **Configuration**: TOML config files included in rootfs

## Performance

Compared to the Python version:
- **Faster Startup**: ~10x faster application startup
- **Lower Memory**: ~50% less memory usage
- **Better Performance**: Native C++ performance
- **Smaller Binary**: Single executable, no Python runtime

## Deployment

### Raspberry Pi Zero
1. Build image with Buildroot
2. Flash to SD card
3. Boot and connect via USB or WiFi
4. Access web interface at `http://192.168.3.1:8000`

### Default Credentials
- Username: `admin`
- Password: `admin` (change immediately)

## Development

### Code Structure
```
cpp/
├── include/          # Header files
├── src/             # Source files
├── CMakeLists.txt   # Build configuration
└── main.cpp         # Application entry point
```

### Key Classes
- `Config`: Singleton configuration manager
- `Database`: SQLite database wrapper
- `CryptoManager`: Encryption/decryption operations
- `AuthManager`: Authentication and sessions
- `StorageManager`: File storage operations
- `WiFiManager`: WiFi network management
- `SystemManager`: System monitoring
- `HttpServer`: HTTP server and routing

## License

MIT License - see LICENSE file for details.
