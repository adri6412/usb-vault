#!/bin/bash
# Complete VaultUSB installation script for DietPi Bookworm

set -e

echo "VaultUSB Installation for DietPi Bookworm"
echo "========================================="

# Check if running on DietPi
if [ ! -f /boot/dietpi/.dietpi_version ]; then
    echo "Error: This script is designed for DietPi"
    echo "Please install DietPi first: https://dietpi.com/"
    exit 1
fi

# Check if it's Bookworm
if ! grep -q "bookworm" /etc/os-release; then
    echo "Error: This script is optimized for DietPi Bookworm"
    echo "Detected OS: $(grep PRETTY_NAME /etc/os-release | cut -d'"' -f2)"
    echo "Please use DietPi Bookworm for optimal compatibility"
    exit 1
fi

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    echo "Error: Please do not run this script as root"
    echo "Run as a regular user with sudo privileges"
    exit 1
fi

echo "Detected DietPi version: $(cat /boot/dietpi/.dietpi_version)"

# Update system
echo "Updating DietPi system..."
sudo apt-get update
sudo apt-get upgrade -y

# Install essential packages for DietPi Bookworm
echo "Installing essential packages for DietPi Bookworm..."
sudo apt-get install -y \
    python3 \
    python3-pip \
    python3-venv \
    python3-dev \
    python3-wheel \
    build-essential \
    git \
    curl \
    wget \
    unzip \
    dnsmasq \
    hostapd \
    iw \
    wireless-tools \
    wpasupplicant \
    net-tools \
    iputils-ping \
    iptables \
    iptables-persistent \
    python3-cryptography \
    python3-setuptools

# Install DietPi tools if not present
if ! command -v dietpi-software &> /dev/null; then
    echo "Installing DietPi tools..."
    curl -sSL https://github.com/MichaIng/DietPi/raw/master/.build/images/dietpi-installer | sudo bash
fi

# Enable USB gadget mode
echo "Enabling USB gadget mode..."
sudo dietpi-config advanced enable_usb_gadget

# Configure Wi-Fi if not already configured
if [ ! -f /etc/wpa_supplicant/wpa_supplicant.conf ]; then
    echo "Configuring Wi-Fi..."
    sudo dietpi-config
    echo "Please configure Wi-Fi in the DietPi configuration menu"
    echo "Press Enter when done..."
    read
fi

# Clone VaultUSB repository
echo "Cloning VaultUSB repository..."
if [ -d "vault-usb" ]; then
    echo "VaultUSB directory already exists, updating..."
    cd vault-usb
    git pull
else
    git clone https://github.com/yourusername/vault-usb.git
    cd vault-usb
fi

# Make scripts executable
chmod +x scripts/*.sh

# Run bootstrap
echo "Running VaultUSB bootstrap..."
./scripts/bootstrap_vault.sh

# Setup networking
echo "Setting up networking..."
sudo ./scripts/setup_networking.sh

# Enable USB gadget
echo "Enabling USB Ethernet gadget..."
sudo ./scripts/enable_usb_gadget.sh

# Generate TLS certificate
echo "Generating TLS certificate..."
./scripts/make_cert.sh

# Configure DietPi services
echo "Configuring DietPi services..."

# Disable conflicting services
sudo systemctl disable dnsmasq
sudo systemctl disable hostapd

# Enable VaultUSB services
sudo systemctl enable vaultusb
sudo systemctl enable uap0-setup
sudo systemctl enable hostapd
sudo systemctl enable dnsmasq@usb0
sudo systemctl enable dnsmasq@uap0

# Start services
echo "Starting VaultUSB services..."
sudo systemctl start vaultusb
sudo systemctl start uap0-setup
sudo systemctl start hostapd
sudo systemctl start dnsmasq@usb0
sudo systemctl start dnsmasq@uap0

# Configure DietPi to start VaultUSB on boot
echo "Configuring DietPi startup..."
sudo tee /etc/systemd/system/dietpi-vaultusb.service > /dev/null << 'EOF'
[Unit]
Description=DietPi VaultUSB Startup
After=multi-user.target

[Service]
Type=oneshot
ExecStart=/bin/bash -c 'systemctl start vaultusb uap0-setup hostapd dnsmasq@usb0 dnsmasq@uap0'
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl enable dietpi-vaultusb.service

# Create DietPi menu entry
echo "Creating DietPi menu entry..."
sudo tee /etc/dietpi/dietpi-software/desktop/vaultusb.desktop > /dev/null << 'EOF'
[Desktop Entry]
Name=VaultUSB
Comment=USB Vault Management
Exec=xdg-open http://192.168.3.1
Icon=applications-internet
Terminal=false
Type=Application
Categories=Network;
EOF

# Set up log rotation
echo "Setting up log rotation..."
sudo tee /etc/logrotate.d/vaultusb > /dev/null << 'EOF'
/var/log/vaultusb/*.log {
    daily
    missingok
    rotate 7
    compress
    delaycompress
    notifempty
    create 644 vaultusb vaultusb
    postrotate
        systemctl reload vaultusb.service
    endscript
}
EOF

# Create status check script
echo "Creating status check script..."
sudo tee /usr/local/bin/vaultusb-status > /dev/null << 'EOF'
#!/bin/bash
echo "VaultUSB Status Check"
echo "===================="
echo "Service Status:"
systemctl is-active vaultusb uap0-setup hostapd dnsmasq@usb0 dnsmasq@uap0
echo ""
echo "Network Interfaces:"
ip addr show | grep -E "(usb0|uap0|wlan0)"
echo ""
echo "Access URLs:"
echo "USB: http://192.168.3.1"
echo "Wi-Fi AP: http://10.42.0.1"
echo ""
echo "Logs:"
journalctl -u vaultusb --lines=10 --no-pager
EOF

sudo chmod +x /usr/local/bin/vaultusb-status

# Final configuration
echo "Performing final configuration..."

# Set proper permissions
sudo chown -R vaultusb:vaultusb /opt/vaultusb
sudo chmod -R 755 /opt/vaultusb

# Create system info file
echo "Creating system info file..."
sudo tee /opt/vaultusb/system_info.txt > /dev/null << EOF
VaultUSB System Information
==========================
Installation Date: $(date)
DietPi Version: $(cat /boot/dietpi/.dietpi_version)
Kernel Version: $(uname -r)
Python Version: $(python3 --version)
VaultUSB Version: 1.0.0

Access Information:
- USB: http://192.168.3.1
- Wi-Fi AP: http://10.42.0.1 (SSID: VaultUSB, Password: ChangeMeVault!)
- Wi-Fi Client: http://[client-ip]

Default Credentials:
- Username: admin
- Password: admin
- Master Password: admin

IMPORTANT: Change all passwords after first login!
EOF

echo ""
echo "VaultUSB installation complete!"
echo "==============================="
echo ""
echo "System Information:"
echo "DietPi Version: $(cat /boot/dietpi/.dietpi_version)"
echo "Installation Date: $(date)"
echo ""
echo "Access URLs:"
echo "USB: http://192.168.3.1"
echo "Wi-Fi AP: http://10.42.0.1"
echo "Wi-Fi Client: http://[client-ip]"
echo ""
echo "Default Credentials:"
echo "Username: admin"
echo "Password: admin"
echo "Master Password: admin"
echo ""
echo "IMPORTANT: Change all passwords after first login!"
echo ""
echo "Useful Commands:"
echo "Check status: vaultusb-status"
echo "View logs: sudo journalctl -u vaultusb -f"
echo "Restart service: sudo systemctl restart vaultusb"
echo ""
echo "Reboot recommended to ensure all services start properly."
echo "Run: sudo reboot"
