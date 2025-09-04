# VaultUSB Buildroot Setup Guide

This guide provides detailed instructions for creating a custom minimal Linux system for VaultUSB using Buildroot, specifically optimized for the Raspberry Pi Zero W first model.

## Overview

The Buildroot setup creates an ultra-lightweight, single-purpose Linux system that:
- Boots in under 30 seconds
- Uses minimal resources (64MB RAM minimum)
- Has a reduced attack surface
- Is optimized specifically for VaultUSB functionality
- Includes only essential components

## Prerequisites

### Development Machine Requirements
- Linux-based system (Ubuntu 20.04+ recommended)
- At least 8GB free disk space (20GB recommended)
- 4GB RAM minimum (8GB recommended)
- Internet connection for downloading packages

### Software Dependencies
The setup script will automatically install these dependencies:
- build-essential, gcc, g++, make
- libncurses5-dev (for menuconfig)
- git, wget, rsync, unzip
- python3, python3-dev
- cpio, bc, file, which, sed, gawk
- Various archive tools (tar, bzip2, gzip, xz-utils)

## Quick Start

```bash
# Clone the repository
git clone https://github.com/adri6412/usb-vault.git
cd usb-vault

# Run the setup script
chmod +x scripts/setup_buildroot.sh
./scripts/setup_buildroot.sh
```

The script will:
1. Install required dependencies
2. Download Buildroot 2024.02.6
3. Create the VaultUSB configuration
4. Set up the build environment
5. Optionally start the build process

## Manual Setup Steps

If you prefer to set up manually or customize further:

### 1. Download and Extract Buildroot

```bash
mkdir buildroot_workspace
cd buildroot_workspace
wget https://buildroot.org/downloads/buildroot-2024.02.6.tar.xz
tar -xf buildroot-2024.02.6.tar.xz
cd buildroot-2024.02.6
```

### 2. Create External Directory

```bash
cd ..
mkdir br2-external-vaultusb
cd br2-external-vaultusb

# Copy the configuration files created by the script
# (or create them manually following the script)
```

### 3. Configure and Build

```bash
cd ../buildroot-2024.02.6
export BR2_EXTERNAL="../br2-external-vaultusb"
make vaultusb_rpi0w_defconfig
make menuconfig  # Optional customization
make -j$(nproc)
```

## Configuration Details

### Kernel Configuration

The custom kernel configuration includes:
- **USB Gadget Support**: Enables USB Ethernet functionality
- **WiFi Support**: BCM43430 wireless chip (Pi Zero W)
- **Cryptographic API**: Hardware acceleration for encryption
- **Networking**: Bridge, VLAN, netfilter support
- **File Systems**: EXT4 with ACL support
- **Hardware Monitoring**: Temperature sensors, GPIO

Key kernel modules:
```
CONFIG_USB_GADGET=y
CONFIG_USB_ETH=m  
CONFIG_BRCMFMAC=y
CONFIG_CRYPTO_CHACHA20POLY1305=y
```

### System Configuration

#### Core Components
- **Init System**: systemd for service management
- **Shell**: Bash for scripting support
- **SSH**: OpenSSH for remote access
- **TLS**: OpenSSL for encryption

#### Networking Stack
- **DHCP**: dhcpcd for client functionality
- **DNS/DHCP Server**: dnsmasq for AP mode
- **WiFi**: hostapd + wpa_supplicant
- **Firewall**: iptables for NAT and filtering

#### Python Environment
- **Python 3.11**: Custom-compiled with minimal modules
- **Required Modules**: ssl, sqlite, hashlib, uuid, decimal
- **Package Manager**: pip for VaultUSB dependencies

### VaultUSB Integration

#### Application Structure
```
/opt/vaultusb/
├── app/                 # VaultUSB application code
├── config.toml         # Configuration file
├── storage/            # Encrypted file storage
├── setup_usb_gadget.sh # USB gadget configuration
└── requirements.txt    # Python dependencies
```

#### Systemd Services
- `vaultusb.service`: Main application service
- `usb-gadget.service`: USB Ethernet gadget setup
- Standard networking services (hostapd, dnsmasq)

#### User and Permissions
- Dedicated `vaultusb` user (UID 1000)
- Secure file permissions
- Service isolation

## Customization Options

### Changing the Configuration

```bash
# Navigate to Buildroot directory
cd buildroot_workspace/buildroot-2024.02.6
export BR2_EXTERNAL="../br2-external-vaultusb"

# Load configuration
make vaultusb_rpi0w_defconfig

# Customize system packages
make menuconfig

# Customize kernel
make linux-menuconfig

# Customize Busybox (if using)
make busybox-menuconfig

# Save changes
make savedefconfig
```

### Adding Custom Packages

Edit `br2-external-vaultusb/Config.in` to add new packages:

```makefile
source "$BR2_EXTERNAL_VAULTUSB_PATH/package/mypackage/Config.in"
```

Create package directory:
```bash
mkdir -p br2-external-vaultusb/package/mypackage
```

### Custom Overlays

Add files to `br2-external-vaultusb/overlay/` to include them in the root filesystem:

```
overlay/
├── etc/
│   ├── systemd/system/  # Custom services
│   ├── network/         # Network configuration
│   └── init.d/          # Init scripts (if not using systemd)
├── opt/
│   └── vaultusb/        # Application files
└── usr/
    └── local/bin/       # Custom scripts
```

## Build Process

### Build Time
- First build: 1-2 hours (downloads and compiles everything)
- Incremental builds: 5-15 minutes (only changed components)
- Clean rebuild: 30-60 minutes (with cached downloads)

### Build Artifacts

After successful build, find artifacts in `output/images/`:
```
output/images/
├── Image                # Linux kernel
├── bcm2708-rpi-zero-w.dtb  # Device tree blob
├── boot.vfat           # Boot partition
├── bootcode.bin        # Raspberry Pi bootloader
├── cmdline.txt         # Kernel command line
├── config.txt          # Raspberry Pi configuration
├── fixup.dat          # GPU firmware
├── rootfs.ext4        # Root filesystem
├── rpi-firmware/      # Additional firmware files
├── sdcard.img         # Complete SD card image
└── start.elf          # GPU firmware
```

### Flashing the Image

```bash
# Find your SD card device
lsblk

# Flash the image (replace /dev/sdX with your SD card)
sudo dd if=output/images/sdcard.img of=/dev/sdX bs=4M status=progress
sync

# Safely remove
sudo eject /dev/sdX
```

## Troubleshooting

### Build Errors

**Error: Missing host dependencies**
```bash
# Install missing packages
sudo apt-get install [missing-package]
# Re-run make
```

**Error: Download failures**
```bash
# Clean download cache
make clean
# Try again
make -j$(nproc)
```

**Error: Out of space**
```bash
# Clean build artifacts
make clean
# Check available space
df -h
```

### Runtime Issues

**USB Gadget not working**
- Check if dwc2 overlay is enabled in config.txt
- Verify USB cable supports data (not power-only)
- Check kernel modules: `lsmod | grep dwc2`

**WiFi not connecting**
- Verify country code in config.txt
- Check wpa_supplicant configuration
- Ensure correct WiFi credentials

**VaultUSB not accessible**
- Check service status: `systemctl status vaultusb`
- Verify network configuration: `ip addr show`
- Check logs: `journalctl -u vaultusb`

### Development and Debugging

**Enable SSH access**
Add to overlay/etc/systemd/system/multi-user.target.wants/:
```bash
ln -s ../sshd.service .
```

**Add debug tools**
In menuconfig, enable:
- Package Selection → Debugging, profiling and benchmark → gdb
- Package Selection → System tools → htop, strace

**Serial console access**
Enable UART in config.txt:
```
enable_uart=1
```
Connect USB-to-serial adapter to GPIO pins 14/15.

## Performance Optimization

### Memory Usage
The default configuration uses approximately:
- Kernel: ~15MB
- Systemd + base system: ~25MB  
- Python + VaultUSB: ~30MB
- Available for storage/cache: ~60MB+ (256MB Pi Zero W)

### Storage Optimization
- Root filesystem: ~200MB
- Available for user files: Remaining SD card space
- Consider read-only root filesystem for better reliability

### Boot Time Optimization
Current boot time: ~25-30 seconds
Further optimizations possible:
- Reduce kernel size (remove unused drivers)
- Minimize systemd services
- Use faster SD card (Class 10/U3)

## Security Considerations

### Build-time Security
- All packages compiled with security flags
- Minimal attack surface (only required packages)
- No unnecessary services enabled

### Runtime Security
- Dedicated user accounts
- Service isolation
- Firewall rules for network access
- Encrypted storage for sensitive data

### Network Security
- Default WiFi AP password should be changed
- USB Ethernet isolated from WiFi networks
- NAT rules configured for secure routing

## Advanced Configuration

### Custom Kernel Patches

Create patches in `br2-external-vaultusb/patches/linux/`:
```bash
mkdir -p br2-external-vaultusb/patches/linux
# Add .patch files here
```

### Custom Init Scripts

For non-systemd initialization, add scripts to:
```
overlay/etc/init.d/
overlay/etc/rc.d/
```

### Read-only Root Filesystem

Enable in menuconfig:
```
System configuration → 
  Root filesystem overlay directories → (add overlay-ro)
  Custom scripts to run after creating filesystem images → (add script)
```

## Maintenance and Updates

### Updating Buildroot
```bash
# Download new version
wget https://buildroot.org/downloads/buildroot-YYYY.MM.N.tar.xz

# Extract and migrate configuration
# (Manual process - review changes)
```

### Updating VaultUSB
```bash
# Update source code
git pull origin main

# Rebuild with new code
make -j$(nproc)
```

### Package Updates
Buildroot manages package versions automatically, but you can:
- Check for newer package versions in menuconfig
- Update custom packages in br2-external directory

## Contributing

To contribute to the Buildroot configuration:

1. Test your changes thoroughly
2. Document configuration changes
3. Update this README if needed
4. Submit pull request with detailed description

## Support

For Buildroot-specific issues:
1. Check Buildroot manual: https://buildroot.org/docs.html
2. Review build logs in `output/build/`
3. Check configuration with `make show-targets`
4. Ask on Buildroot mailing list for complex issues

For VaultUSB application issues, refer to the main README.md.