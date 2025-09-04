#!/bin/bash
# Setup networking for VaultUSB on DietPi

set -e

echo "Setting up networking for VaultUSB on DietPi..."

# Check if running on DietPi
if [ ! -f /boot/dietpi/.dietpi_version ]; then
    echo "Warning: This script is optimized for DietPi"
    echo "Continuing anyway..."
fi

# Install required packages
echo "Installing required packages..."
sudo apt-get update
sudo apt-get install -y dnsmasq hostapd iw wireless-tools wpasupplicant

# DietPi specific: ensure networking tools are available
sudo apt-get install -y net-tools iputils-ping

# Stop services if running
sudo systemctl stop dnsmasq
sudo systemctl stop hostapd

# Configure dhcpcd
echo "Configuring dhcpcd..."
if ! grep -q "VaultUSB DHCP configuration" /etc/dhcpcd.conf; then
    echo "" | sudo tee -a /etc/dhcpcd.conf
    echo "# VaultUSB DHCP configuration" | sudo tee -a /etc/dhcpcd.conf
    cat networking/dhcpcd.append.conf | sudo tee -a /etc/dhcpcd.conf
fi

# DietPi specific: ensure dhcpcd is enabled
sudo systemctl enable dhcpcd

# Configure dnsmasq
echo "Configuring dnsmasq..."
sudo mkdir -p /etc/dnsmasq.d
sudo cp networking/dnsmasq-usb0.conf /etc/dnsmasq.d/
sudo cp networking/dnsmasq-uap0.conf /etc/dnsmasq.d/

# Configure hostapd
echo "Configuring hostapd..."
sudo mkdir -p /etc/hostapd
sudo cp networking/hostapd.conf /etc/hostapd/

# Update hostapd configuration
sudo sed -i 's|#DAEMON_CONF=""|DAEMON_CONF="/etc/hostapd/hostapd.conf"|' /etc/default/hostapd

# Enable IP forwarding
echo "Enabling IP forwarding..."
sudo sed -i 's/#net.ipv4.ip_forward=1/net.ipv4.ip_forward=1/' /etc/sysctl.conf

# Configure iptables for NAT
echo "Configuring iptables..."
sudo iptables -t nat -A POSTROUTING -o wlan0 -j MASQUERADE
sudo iptables -A FORWARD -i wlan0 -o uap0 -m state --state RELATED,ESTABLISHED -j ACCEPT
sudo iptables -A FORWARD -i uap0 -o wlan0 -j ACCEPT

# Save iptables rules
sudo iptables-save | sudo tee /etc/iptables/rules.v4 > /dev/null

# Install iptables-persistent if not present
if ! dpkg -l | grep -q iptables-persistent; then
    sudo apt-get install -y iptables-persistent
fi

# DietPi specific: configure iptables rules persistence
sudo mkdir -p /etc/iptables

# Copy systemd service files
echo "Installing systemd services..."
sudo cp systemd/dnsmasq@.service /etc/systemd/system/
sudo cp systemd/hostapd.service /etc/systemd/system/
sudo cp systemd/uap0-setup.service /etc/systemd/system/

# Reload systemd
sudo systemctl daemon-reload

# Enable services
sudo systemctl enable uap0-setup.service
sudo systemctl enable hostapd.service
sudo systemctl enable dnsmasq@usb0.service
sudo systemctl enable dnsmasq@uap0.service

echo "Networking setup complete!"
echo "Services will start on next boot."
echo "You can start them now with:"
echo "  sudo systemctl start uap0-setup"
echo "  sudo systemctl start hostapd"
echo "  sudo systemctl start dnsmasq@usb0"
echo "  sudo systemctl start dnsmasq@uap0"
