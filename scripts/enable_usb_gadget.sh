#!/bin/bash
# Enable USB Ethernet gadget on Raspberry Pi Zero for DietPi

set -e

echo "Enabling USB Ethernet gadget for DietPi..."

# Check if running on DietPi
if [ ! -f /boot/dietpi/.dietpi_version ]; then
    echo "Warning: This script is optimized for DietPi"
    echo "Continuing anyway..."
fi

# Check if running on Raspberry Pi
if ! grep -q "Raspberry Pi" /proc/cpuinfo; then
    echo "Warning: This script is designed for Raspberry Pi"
fi

# Enable dwc2 overlay
echo "dtoverlay=dwc2" | sudo tee -a /boot/config.txt

# Enable g_ether module
echo "g_ether" | sudo tee -a /etc/modules

# Create udev rule for USB gadget
sudo tee /etc/udev/rules.d/99-usb-gadget.rules > /dev/null << 'EOF'
# USB Ethernet gadget
SUBSYSTEM=="usb", ATTRS{idVendor}=="1d6b", ATTRS{idProduct}=="0104", MODE="0666"
EOF

# Reload udev rules
sudo udevadm control --reload-rules
sudo udevadm trigger

echo "USB Ethernet gadget enabled. Reboot required."
echo "After reboot, the Pi will appear as a USB Ethernet device."
