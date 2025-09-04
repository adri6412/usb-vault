# VaultUSB - Raspberry Pi Zero USB Vault (DietPi Bookworm Compatible)

A secure, encrypted file storage system running on Raspberry Pi Zero with Wi-Fi management and system update capabilities. Optimized for DietPi Bookworm lightweight Linux distribution.

## Features

### 🔒 Security
- Application-level encryption using XChaCha20-Poly1305
- Master key sealed with Argon2id password hashing
- Per-file encryption keys derived via HKDF
- Secure file deletion with multiple overwrites
- Optional TOTP 2FA authentication
- Auto-lock after configurable idle timeout

### 🌐 Networking
- USB Ethernet gadget (usb0) with static IP 192.168.3.1/24
- Wi-Fi client (wlan0) for Internet access
- Simultaneous Wi-Fi hotspot (uap0) with IP 10.42.0.1/24
- DHCP servers for both interfaces
- NAT routing between interfaces

### 📁 File Management
- Web-based file upload/download/preview
- Encrypted storage with random filenames
- File metadata stored in SQLite database
- Search and sort capabilities
- Drag-and-drop upload interface

### 📶 Wi-Fi Management
- Scan and connect to Wi-Fi networks
- Save and manage network credentials
- Real-time connection status
- Atomic connection with rollback on failure

### 🔧 System Management
- Check for available system updates
- Perform full system upgrades
- Raspberry Pi firmware updates
- System status monitoring
- Remote reboot capability

## Hardware Requirements

- Raspberry Pi Zero W (or Zero 2 W)
- MicroSD card (8GB minimum, 32GB recommended)
- USB cable for power and data

## Software Requirements

- DietPi Bookworm (latest version recommended)
- Python 3.11+
- User with sudo privileges
- Debian Bookworm base system

## Installation

### Quick Installation (Recommended)

For the easiest installation on DietPi Bookworm, use the automated installer:

```bash
# Download and run the DietPi Bookworm installer
curl -sSL https://raw.githubusercontent.com/yourusername/vault-usb/main/scripts/install_dietpi.sh | bash
```

### Manual Installation

### 1. Flash DietPi Bookworm

Download and flash DietPi Bookworm to your microSD card using the DietPi Imager or Raspberry Pi Imager.

### 2. Initial DietPi Bookworm Setup

Boot your Pi and run the DietPi configuration:

```bash
sudo dietpi-config
```

Configure:
- Change password
- Enable SSH
- Configure Wi-Fi (optional)
- Enable USB gadget mode
- Ensure Python 3.11 is installed

### 3. Boot and Connect

Connect via SSH:

```bash
ssh dietpi@[pi-ip-address]
```

### 4. Clone and Setup VaultUSB

```bash
# Clone the repository
git clone https://github.com/yourusername/vault-usb.git
cd vault-usb

# Make scripts executable
chmod +x scripts/*.sh

# Run bootstrap script
./scripts/bootstrap_vault.sh
```

### 5. Configure Networking

```bash
# Enable USB gadget
sudo ./scripts/enable_usb_gadget.sh

# Setup networking
sudo ./scripts/setup_networking.sh

# Generate TLS certificate (optional)
./scripts/make_cert.sh
```

### 6. Start Services

```bash
# Start VaultUSB service
sudo systemctl start vaultusb

# Start networking services
sudo systemctl start uap0-setup
sudo systemctl start hostapd
sudo systemctl start dnsmasq@usb0
sudo systemctl start dnsmasq@uap0

# Enable services to start on boot
sudo systemctl enable vaultusb uap0-setup hostapd dnsmasq@usb0 dnsmasq@uap0
```

### 7. Reboot

```bash
sudo reboot
```

## Access

After setup, you can access VaultUSB through:

- **USB Connection**: http://192.168.3.1
- **Wi-Fi AP**: http://10.42.0.1 (SSID: VaultUSB, Password: ChangeMeVault!)
- **Wi-Fi Client**: http://[client-ip-address]

## Default Credentials

- **Username**: admin
- **Password**: admin
- **Master Password**: admin

**⚠️ IMPORTANT**: Change all passwords after first login!

## Usage

### First Login

1. Connect to VaultUSB via USB or Wi-Fi
2. Login with default credentials
3. Unlock the vault with the master password
4. Change all passwords in the settings

### File Management

1. Navigate to the Files section
2. Upload files by dragging and dropping or clicking "Upload File"
3. Files are automatically encrypted and stored securely
4. Use the search function to find specific files
5. Preview supported file types directly in the browser

### Wi-Fi Management

1. Go to the Wi-Fi section
2. Click "Scan Networks" to find available networks
3. Click "Connect" on your desired network
4. Enter the password if required
5. The Pi will connect and remember the network

### System Updates

1. Navigate to the System section
2. Click "Check Updates" to see available packages
3. Click "Upgrade System" to install updates
4. Reboot if required

## Configuration

### Network Settings

Edit `/opt/vaultusb/config.toml` to modify:

- IP addresses and subnets
- DHCP ranges
- Wi-Fi AP settings
- Security parameters

### Security Settings

- Idle timeout (default: 10 minutes)
- Password hashing parameters
- Encryption settings
- TLS configuration

## Troubleshooting

### Service Status

```bash
# Check VaultUSB service
sudo systemctl status vaultusb

# Check networking services
sudo systemctl status hostapd
sudo systemctl status dnsmasq@usb0
sudo systemctl status dnsmasq@uap0

# View logs
sudo journalctl -u vaultusb -f

# Use the status check script
vaultusb-status
```

### Network Issues

```bash
# Check interfaces
ip addr show

# Check Wi-Fi status
iwconfig

# Restart networking
sudo systemctl restart hostapd
sudo systemctl restart dnsmasq@usb0
sudo systemctl restart dnsmasq@uap0
```

### USB Gadget Issues

```bash
# Check if USB gadget is loaded
lsmod | grep g_ether

# Check dmesg for errors
dmesg | grep dwc2
dmesg | grep g_ether
```

### Application Issues

```bash
# Check application logs
tail -f /var/log/vaultusb/vaultusb.log

# Restart application
sudo systemctl restart vaultusb

# Check database
sqlite3 /opt/vaultusb/vault.db ".tables"
```

## Security Considerations

1. **Change Default Passwords**: Always change the default admin password and master key password
2. **Use Strong Passwords**: Use complex passwords for all accounts
3. **Enable TOTP**: Enable two-factor authentication for additional security
4. **Regular Updates**: Keep the system updated with the latest security patches
5. **Network Security**: Change the default Wi-Fi AP password
6. **Physical Security**: Keep the device in a secure location

## File Structure

```
vault-usb/
├── app/                    # Main application code
│   ├── __init__.py
│   ├── main.py            # FastAPI application
│   ├── auth.py            # Authentication
│   ├── crypto.py          # Encryption/decryption
│   ├── storage.py         # File storage
│   ├── wifi.py            # Wi-Fi management
│   ├── wifi_helper.py     # Wi-Fi helper functions
│   ├── system.py          # System management
│   ├── db.py              # Database operations
│   ├── models.py          # Database models
│   ├── config.py          # Configuration
│   ├── schemas.py         # API schemas
│   ├── templates/         # HTML templates
│   └── static/            # CSS/JS files
├── scripts/               # Setup scripts
├── systemd/               # Systemd service files
├── networking/            # Network configuration
├── config.toml           # Main configuration
├── requirements.txt      # Python dependencies
└── README.md            # This file
```

## API Endpoints

### Authentication
- `POST /api/auth/login` - User login
- `POST /api/auth/logout` - User logout
- `POST /api/auth/change-password` - Change password
- `POST /api/auth/setup-totp` - Setup TOTP
- `POST /api/auth/verify-totp` - Verify TOTP

### Vault Management
- `POST /api/vault/unlock` - Unlock vault
- `POST /api/vault/lock` - Lock vault
- `GET /api/vault/status` - Get vault status

### File Management
- `GET /api/files` - List files
- `POST /api/files/upload` - Upload file
- `GET /api/files/{file_id}/download` - Download file
- `GET /api/files/{file_id}/preview` - Preview file
- `DELETE /api/files/{file_id}` - Delete file

### Wi-Fi Management
- `GET /api/wifi/networks` - Scan networks
- `GET /api/wifi/status` - Get Wi-Fi status
- `POST /api/wifi/connect` - Connect to network
- `POST /api/wifi/disconnect` - Disconnect
- `DELETE /api/wifi/networks/{ssid}` - Forget network

### System Management
- `GET /api/system/status` - Get system status
- `GET /api/system/updates` - Check updates
- `POST /api/system/upgrade` - Upgrade system
- `POST /api/system/reboot` - Reboot system

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Support

For support and questions:

1. Check the troubleshooting section
2. Review the logs for error messages
3. Create an issue on GitHub
4. Check the documentation

## DietPi Bookworm Specific Features

### DietPi Bookworm Integration
- Optimized for DietPi Bookworm lightweight distribution
- Automated installation script
- DietPi menu integration
- System status monitoring
- Optimized resource usage for Bookworm
- Python 3.11 compatibility

### DietPi Bookworm Commands
```bash
# Check VaultUSB status
vaultusb-status

# Access DietPi configuration
sudo dietpi-config

# View system information
dietpi-software

# Check DietPi version
cat /boot/dietpi/.dietpi_version

# Check Debian version
cat /etc/os-release | grep VERSION
```

### DietPi Bookworm Optimization
- Reduced memory footprint (256MB limit)
- Optimized Python 3.11 dependencies
- Streamlined service configuration
- DietPi-specific networking setup
- Enhanced logging and monitoring
- Bookworm-specific package management

## Changelog

### v1.0.0
- Initial release
- USB Ethernet gadget support
- Wi-Fi AP + Client mode
- Encrypted file storage
- Web-based management interface
- System update management
- TOTP 2FA support
- DietPi compatibility
