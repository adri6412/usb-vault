#!/bin/bash
# Bootstrap VaultUSB application for DietPi

set -e

echo "Bootstrapping VaultUSB for DietPi..."

# Check if running on DietPi Bookworm
if [ ! -f /boot/dietpi/.dietpi_version ]; then
    echo "Warning: This script is optimized for DietPi"
    echo "Continuing anyway..."
else
    DIETPI_VERSION=$(cat /boot/dietpi/.dietpi_version)
    echo "Detected DietPi version: $DIETPI_VERSION"
    
    # Check if it's Bookworm
    if ! grep -q "bookworm" /etc/os-release; then
        echo "Warning: This script is optimized for DietPi Bookworm"
        echo "Detected OS: $(grep PRETTY_NAME /etc/os-release | cut -d'"' -f2)"
        echo "Continuing anyway..."
    else
        echo "DietPi Bookworm detected - optimal configuration will be applied"
    fi
fi

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    echo "Please do not run this script as root"
    exit 1
fi

# Create vaultusb user if it doesn't exist
if ! id "vaultusb" &>/dev/null; then
    echo "Creating vaultusb user..."
    sudo useradd -m -s /bin/bash vaultusb
    sudo usermod -a -G sudo vaultusb
fi

# Create application directory
echo "Creating application directory..."
sudo mkdir -p /opt/vaultusb
sudo chown vaultusb:vaultusb /opt/vaultusb

# Copy application files
echo "Copying application files..."
cp -r app /opt/vaultusb/
cp -r scripts /opt/vaultusb/
cp -r systemd /opt/vaultusb/
cp -r networking /opt/vaultusb/
cp requirements.txt /opt/vaultusb/
cp README.md /opt/vaultusb/
cp LICENSE /opt/vaultusb/

# Use appropriate configuration file
if grep -q "bookworm" /etc/os-release; then
    echo "Using DietPi Bookworm configuration..."
    cp config_bookworm.toml /opt/vaultusb/config.toml
else
    echo "Using standard configuration..."
    cp config.toml /opt/vaultusb/
fi

# Set permissions
sudo chown -R vaultusb:vaultusb /opt/vaultusb
sudo chmod +x /opt/vaultusb/scripts/*.sh

# Create Python virtual environment
echo "Creating Python virtual environment..."
cd /opt/vaultusb
python3 -m venv venv
source venv/bin/activate

# Install Python dependencies
echo "Installing Python dependencies for DietPi Bookworm..."
# DietPi Bookworm uses apt for Python packages, install system packages first
sudo apt-get update
sudo apt-get install -y python3-pip python3-venv python3-dev build-essential python3-wheel

# Install Python dependencies
pip install --upgrade pip setuptools wheel
pip install -r requirements.txt

# DietPi Bookworm specific: ensure cryptography is properly installed
pip install --upgrade cryptography

# Create necessary directories
echo "Creating necessary directories..."
mkdir -p /opt/vaultusb/vault
mkdir -p /opt/vaultusb/logs
mkdir -p /var/log/vaultusb

# Set permissions
sudo chown -R vaultusb:vaultusb /opt/vaultusb
sudo chown -R vaultusb:vaultusb /var/log/vaultusb

# Initialize database
echo "Initializing database..."
cd /opt/vaultusb
source venv/bin/activate
python -c "
from app.db import init_database
import asyncio
asyncio.run(init_database())
print('Database initialized')
"

# Create master key (will be sealed with default password)
echo "Creating master key..."
cd /opt/vaultusb
source venv/bin/activate
python -c "
from app.crypto import crypto_manager
import secrets

# Generate master key
master_key = crypto_manager.generate_master_key()

# Seal with default password
sealed_data = crypto_manager.seal_master_key(master_key, 'admin')

# Save sealed key
with open('master.key', 'w') as f:
    f.write(sealed_data)

print('Master key created and sealed with default password: admin')
print('IMPORTANT: Change the master password after first login!')
"

# Install systemd service
echo "Installing systemd service..."
sudo cp systemd/vaultusb.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable vaultusb.service

# Create log rotation
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

# Set up sudoers
echo "Setting up sudoers..."
sudo tee /etc/sudoers.d/vaultusb > /dev/null << 'EOF'
# VaultUSB sudoers configuration
vaultusb ALL=(ALL) NOPASSWD: /usr/bin/apt-get, /usr/bin/rpi-update, /usr/bin/raspi-config, /sbin/reboot, /usr/sbin/iw, /usr/sbin/wpa_cli, /usr/sbin/hostapd, /usr/sbin/dnsmasq
EOF

sudo chmod 440 /etc/sudoers.d/vaultusb

echo ""
echo "VaultUSB bootstrap complete!"
echo ""
echo "Default credentials:"
echo "  Username: admin"
echo "  Password: admin"
echo "  Master Password: admin"
echo ""
echo "IMPORTANT: Change all passwords after first login!"
echo ""
echo "Access URLs:"
echo "  USB: http://192.168.3.1"
echo "  Wi-Fi AP: http://10.42.0.1"
echo "  Wi-Fi Client: http://[client-ip]"
echo ""
echo "To start the service:"
echo "  sudo systemctl start vaultusb"
echo ""
echo "To view logs:"
echo "  sudo journalctl -u vaultusb -f"
echo ""
echo "Reboot recommended to ensure all services start properly."
