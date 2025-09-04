#!/bin/bash
# VaultUSB Buildroot Setup Script for Raspberry Pi Zero W (first model)
# This script downloads and configures Buildroot to create a minimal Linux system
# optimized for the VaultUSB encrypted file storage system.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILDROOT_VERSION="2024.02.6"
BUILDROOT_DIR="buildroot-${BUILDROOT_VERSION}"
BUILDROOT_URL="https://buildroot.org/downloads/${BUILDROOT_DIR}.tar.xz"
WORK_DIR="$(pwd)/buildroot_workspace"
VAULTUSB_DIR="$(pwd)"
TARGET_ARCH="arm"
TARGET_CPU="arm1176jzf-s"  # ARM11 core used in BCM2835 (Pi Zero W)

# Functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running as root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        log_error "This script should not be run as root"
        exit 1
    fi
}

# Install required packages for building Buildroot
install_dependencies() {
    log_info "Installing dependencies for Buildroot..."
    
    if command -v apt-get &> /dev/null; then
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            libncurses5-dev \
            libncursesw5-dev \
            git \
            bzr \
            cvs \
            mercurial \
            subversion \
            libc6-dev \
            gcc \
            g++ \
            make \
            patch \
            perl \
            python3 \
            python3-dev \
            rsync \
            unzip \
            wget \
            cpio \
            bc \
            file \
            which \
            sed \
            gawk \
            binutils \
            tar \
            bzip2 \
            gzip \
            xz-utils \
            texinfo \
            gettext
    elif command -v dnf &> /dev/null; then
        sudo dnf install -y \
            gcc \
            gcc-c++ \
            make \
            ncurses-devel \
            git \
            patch \
            perl \
            python3 \
            python3-devel \
            rsync \
            unzip \
            wget \
            cpio \
            bc \
            which \
            sed \
            gawk \
            tar \
            bzip2 \
            gzip \
            xz \
            texinfo \
            gettext
    else
        log_error "Package manager not supported. Please install Buildroot dependencies manually."
        exit 1
    fi
}

# Download and extract Buildroot
download_buildroot() {
    log_info "Setting up workspace directory..."
    mkdir -p "${WORK_DIR}"
    cd "${WORK_DIR}"
    
    if [ ! -d "${BUILDROOT_DIR}" ]; then
        log_info "Downloading Buildroot ${BUILDROOT_VERSION}..."
        wget "${BUILDROOT_URL}" -O "${BUILDROOT_DIR}.tar.xz"
        
        log_info "Extracting Buildroot..."
        tar -xf "${BUILDROOT_DIR}.tar.xz"
        rm "${BUILDROOT_DIR}.tar.xz"
    else
        log_info "Buildroot already downloaded"
    fi
    
    cd "${BUILDROOT_DIR}"
}

# Create custom defconfig for Raspberry Pi Zero W with VaultUSB
create_defconfig() {
    log_info "Creating custom defconfig for VaultUSB..."
    
    cat > configs/vaultusb_rpi0w_defconfig << 'EOF'
# Buildroot configuration for VaultUSB on Raspberry Pi Zero W
BR2_arm=y
BR2_arm1176jzf_s=y

# Target options
BR2_TARGET_GENERIC_HOSTNAME="vaultusb"
BR2_TARGET_GENERIC_ISSUE="Welcome to VaultUSB on Buildroot"
BR2_TARGET_GENERIC_PASSWD_SHA256=y

# System configuration
BR2_SYSTEM_DHCP="eth0"
BR2_SYSTEM_DEFAULT_PATH="/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin:/opt/vaultusb"
BR2_ROOTFS_OVERLAY="$(BR2_EXTERNAL_VAULTUSB_PATH)/overlay"
BR2_ROOTFS_POST_BUILD_SCRIPT="$(BR2_EXTERNAL_VAULTUSB_PATH)/post_build.sh"

# Kernel
BR2_LINUX_KERNEL=y
BR2_LINUX_KERNEL_CUSTOM_TARBALL=y
BR2_LINUX_KERNEL_CUSTOM_TARBALL_LOCATION="https://github.com/raspberrypi/linux/archive/refs/heads/rpi-6.6.y.tar.gz"
BR2_LINUX_KERNEL_DEFCONFIG="bcmrpi"
BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="$(BR2_EXTERNAL_VAULTUSB_PATH)/kernel.config"
BR2_LINUX_KERNEL_DTS_SUPPORT=y
BR2_LINUX_KERNEL_INTREE_DTS_NAME="bcm2708-rpi-zero-w"

# Bootloader
BR2_TARGET_RPI_FIRMWARE=y
BR2_TARGET_RPI_FIRMWARE_VARIANT_PI=y
BR2_TARGET_RPI_FIRMWARE_CONFIG_FILE="$(BR2_EXTERNAL_VAULTUSB_PATH)/config.txt"

# Filesystem images
BR2_TARGET_ROOTFS_EXT2=y
BR2_TARGET_ROOTFS_EXT2_4=y
BR2_TARGET_ROOTFS_EXT2_SIZE="512M"

# Toolchain
BR2_TOOLCHAIN_BUILDROOT_CXX=y
BR2_TOOLCHAIN_BUILDROOT_LOCALE=y
BR2_TOOLCHAIN_BUILDROOT_WCHAR=y

# System tools
BR2_PACKAGE_BUSYBOX_CONFIG="$(BR2_EXTERNAL_VAULTUSB_PATH)/busybox.config"
BR2_PACKAGE_BASH=y
BR2_PACKAGE_SUDO=y
BR2_PACKAGE_OPENSSH=y
BR2_PACKAGE_OPENSSL=y

# Development tools
BR2_PACKAGE_MAKE=y
BR2_PACKAGE_CMAKE=y
BR2_PACKAGE_GIT=y

# Networking
BR2_PACKAGE_DHCPCD=y
BR2_PACKAGE_DNSMASQ=y
BR2_PACKAGE_HOSTAPD=y
BR2_PACKAGE_IW=y
BR2_PACKAGE_WIRELESS_TOOLS=y
BR2_PACKAGE_WPA_SUPPLICANT=y
BR2_PACKAGE_WPA_SUPPLICANT_AP_SUPPORT=y
BR2_PACKAGE_WPA_SUPPLICANT_WIFI_DISPLAY=y
BR2_PACKAGE_WPA_SUPPLICANT_MESH_NETWORKING=y
BR2_PACKAGE_WPA_SUPPLICANT_HOTSPOT=y
BR2_PACKAGE_IPTABLES=y
BR2_PACKAGE_BRIDGE_UTILS=y

# USB Gadget support
BR2_PACKAGE_LIBUSB=y
BR2_PACKAGE_LIBUSB_COMPAT=y
BR2_PACKAGE_USBUTILS=y

# Python and libraries
BR2_PACKAGE_PYTHON3=y
BR2_PACKAGE_PYTHON3_BZIP2=y
BR2_PACKAGE_PYTHON3_CURSES=y
BR2_PACKAGE_PYTHON3_PYEXPAT=y
BR2_PACKAGE_PYTHON3_READLINE=y
BR2_PACKAGE_PYTHON3_SSL=y
BR2_PACKAGE_PYTHON3_UNICODEDATA=y
BR2_PACKAGE_PYTHON3_SQLITE=y
BR2_PACKAGE_PYTHON3_ZLIB=y
BR2_PACKAGE_PYTHON3_HASHLIB=y
BR2_PACKAGE_PYTHON3_UUID=y
BR2_PACKAGE_PYTHON3_DECIMAL=y

# Python packages needed for VaultUSB
BR2_PACKAGE_PYTHON_PIP=y
BR2_PACKAGE_PYTHON_SETUPTOOLS=y
BR2_PACKAGE_PYTHON_WHEEL=y

# Cryptography support
BR2_PACKAGE_LIBSODIUM=y
BR2_PACKAGE_CRYPTODEV_LINUX=y

# Database
BR2_PACKAGE_SQLITE=y

# File system tools
BR2_PACKAGE_E2FSPROGS=y
BR2_PACKAGE_E2FSPROGS_FSCK=y

# System monitoring
BR2_PACKAGE_HTOP=y
BR2_PACKAGE_SYSSTAT=y
BR2_PACKAGE_LSOF=y

# Time synchronization
BR2_PACKAGE_NTP=y

# Compression
BR2_PACKAGE_GZIP=y
BR2_PACKAGE_BZIP2=y
BR2_PACKAGE_XZ=y

# Text processing
BR2_PACKAGE_SED=y
BR2_PACKAGE_GREP=y

# Init system
BR2_INIT_SYSTEMD=y
BR2_PACKAGE_SYSTEMD=y
BR2_PACKAGE_SYSTEMD_NETWORKD=y
BR2_PACKAGE_SYSTEMD_RESOLVED=y
BR2_PACKAGE_SYSTEMD_LOGIND=y
EOF

    log_success "Custom defconfig created"
}

# Create external directory structure
create_external_structure() {
    log_info "Creating external directory structure..."
    
    mkdir -p br2-external-vaultusb
    cd br2-external-vaultusb
    
    # Create external.mk
    cat > external.mk << 'EOF'
include $(sort $(wildcard $(BR2_EXTERNAL_VAULTUSB_PATH)/package/*/*.mk))
EOF
    
    # Create external.desc
    cat > external.desc << 'EOF'
name: VAULTUSB
desc: VaultUSB external packages and configurations for Buildroot
EOF
    
    # Create Config.in
    cat > Config.in << 'EOF'
source "$BR2_EXTERNAL_VAULTUSB_PATH/package/vaultusb/Config.in"
EOF
    
    # Create package directory structure
    mkdir -p package/vaultusb
    
    # Create VaultUSB package Config.in
    cat > package/vaultusb/Config.in << 'EOF'
config BR2_PACKAGE_VAULTUSB
	bool "vaultusb"
	depends on BR2_PACKAGE_PYTHON3
	select BR2_PACKAGE_PYTHON_PIP
	help
	  VaultUSB - Secure encrypted file storage system for Raspberry Pi Zero W
	  
	  https://github.com/adri6412/usb-vault

comment "vaultusb needs python3"
	depends on !BR2_PACKAGE_PYTHON3
EOF
    
    # Create VaultUSB package makefile
    cat > package/vaultusb/vaultusb.mk << 'EOF'
################################################################################
#
# vaultusb
#
################################################################################

VAULTUSB_VERSION = 1.0.0
VAULTUSB_SITE_METHOD = local
VAULTUSB_SITE = $(BR2_EXTERNAL_VAULTUSB_PATH)/../
VAULTUSB_DEPENDENCIES = python3 python-pip

define VAULTUSB_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/opt/vaultusb
	cp -r $(@D)/app $(TARGET_DIR)/opt/vaultusb/
	cp -r $(@D)/config*.toml $(TARGET_DIR)/opt/vaultusb/
	cp -r $(@D)/requirements.txt $(TARGET_DIR)/opt/vaultusb/
	cp -r $(@D)/networking $(TARGET_DIR)/opt/vaultusb/
	cp -r $(@D)/systemd $(TARGET_DIR)/etc/systemd/system/
	mkdir -p $(TARGET_DIR)/var/log/vaultusb
	mkdir -p $(TARGET_DIR)/opt/vaultusb/storage
	chmod +x $(TARGET_DIR)/opt/vaultusb/app/main.py
endef

define VAULTUSB_USERS
	vaultusb -1 vaultusb -1 * /opt/vaultusb /bin/bash - VaultUSB user
endef

$(eval $(generic-package))
EOF

    log_success "External directory structure created"
}

# Create kernel configuration fragment
create_kernel_config() {
    log_info "Creating kernel configuration fragment..."
    
    cat > kernel.config << 'EOF'
# USB Gadget support for VaultUSB
CONFIG_USB_GADGET=y
CONFIG_USB_GADGET_VBUS_DRAW=500
CONFIG_USB_GADGET_SELECTED=y
CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS=2

# USB Ethernet Gadget
CONFIG_USB_ETH=m
CONFIG_USB_ETH_RNDIS=y
CONFIG_USB_ETH_EEM=y

# USB CDC Ethernet function
CONFIG_USB_F_ECM=m
CONFIG_USB_F_SUBSET=m
CONFIG_USB_F_RNDIS=m
CONFIG_USB_F_EEM=m

# Composite USB Gadget
CONFIG_USB_G_MULTI=m

# DWC2 USB controller (BCM2835)
CONFIG_USB_DWC2=y
CONFIG_USB_DWC2_HOST=y
CONFIG_USB_DWC2_PERIPHERAL=y
CONFIG_USB_DWC2_DUAL_ROLE=y

# WiFi support
CONFIG_CFG80211=y
CONFIG_CFG80211_WEXT=y
CONFIG_MAC80211=y
CONFIG_BRCMFMAC=y
CONFIG_BRCMFMAC_SDIO=y

# Networking
CONFIG_NETFILTER=y
CONFIG_NETFILTER_ADVANCED=y
CONFIG_NF_CONNTRACK=y
CONFIG_NETFILTER_XT_MATCH_CONNTRACK=y
CONFIG_NETFILTER_XT_TARGET_MASQUERADE=y
CONFIG_NF_NAT=y
CONFIG_NF_NAT_IPV4=y

# Bridge support
CONFIG_BRIDGE=y
CONFIG_BRIDGE_NETFILTER=y

# VLAN support
CONFIG_VLAN_8021Q=y

# File systems
CONFIG_EXT4_FS=y
CONFIG_EXT4_FS_POSIX_ACL=y
CONFIG_EXT4_FS_SECURITY=y

# Cryptographic API
CONFIG_CRYPTO=y
CONFIG_CRYPTO_AEAD=y
CONFIG_CRYPTO_SKCIPHER=y
CONFIG_CRYPTO_HASH=y
CONFIG_CRYPTO_CHACHA20POLY1305=y
CONFIG_CRYPTO_CHACHA20=y
CONFIG_CRYPTO_POLY1305=y
CONFIG_CRYPTO_SHA256=y
CONFIG_CRYPTO_USER=y

# Random number generation
CONFIG_CRYPTO_DRBG_MENU=y
CONFIG_CRYPTO_DRBG_HMAC=y
CONFIG_CRYPTO_JITTERENTROPY=y

# Hardware monitoring
CONFIG_HWMON=y
CONFIG_SENSORS_BCM2835=y

# Thermal management
CONFIG_THERMAL=y
CONFIG_BCM2835_THERMAL=y

# GPIO support
CONFIG_GPIOLIB=y
CONFIG_GPIO_BCM_VIRT=y
CONFIG_GPIO_SYSFS=y

# SPI support
CONFIG_SPI=y
CONFIG_SPI_BCM2835=y
CONFIG_SPI_BCM2835AUX=y

# I2C support
CONFIG_I2C=y
CONFIG_I2C_BCM2835=y

# PWM support
CONFIG_PWM=y
CONFIG_PWM_BCM2835=y

# LED support
CONFIG_LEDS_CLASS=y
CONFIG_LEDS_GPIO=y
CONFIG_LEDS_TRIGGER_HEARTBEAT=y
CONFIG_LEDS_TRIGGER_TIMER=y

# Watchdog
CONFIG_WATCHDOG=y
CONFIG_BCM2835_WDT=y

# Real Time Clock
CONFIG_RTC_CLASS=y
CONFIG_RTC_DRV_DS1307=y
CONFIG_RTC_DRV_PCF8523=y

# Memory management
CONFIG_CMA=y
CONFIG_CMA_SIZE_MBYTES=64
EOF

    log_success "Kernel configuration fragment created"
}

# Create config.txt for Raspberry Pi
create_config_txt() {
    log_info "Creating Raspberry Pi config.txt..."
    
    cat > config.txt << 'EOF'
# VaultUSB Raspberry Pi Zero W Configuration

# GPU Memory Split (minimal for headless operation)
gpu_mem=16

# Enable UART (for debugging if needed)
enable_uart=1

# Enable I2C and SPI
dtparam=i2c_arm=on
dtparam=spi=on

# Enable USB OTG (gadget mode)
dtoverlay=dwc2

# WiFi country (set to your country)
country=US

# Enable camera (if needed)
start_x=0

# Disable boot splash
disable_splash=1

# Enable SSH (allow login over network)
ssh=

# Set hostname
hostname=vaultusb

# Overclock settings for better performance (optional, conservative)
arm_freq=1000
core_freq=500
sdram_freq=500
over_voltage=2

# Enable hardware random number generator
dtparam=random=on

# Enable watchdog
dtparam=watchdog=on

# Device tree overlays
dtoverlay=uart0,txd0_pin=14,rxd0_pin=15,pin_func=4
dtoverlay=gpio-poweroff,gpiopin=21,active_low=1

# USB settings
max_usb_current=1
EOF

    log_success "config.txt created"
}

# Create overlay directory structure
create_overlay() {
    log_info "Creating overlay directory..."
    
    mkdir -p overlay/opt/vaultusb
    mkdir -p overlay/etc/systemd/system
    mkdir -p overlay/etc/systemd/system/multi-user.target.wants
    mkdir -p overlay/etc/udev/rules.d
    mkdir -p overlay/etc/network/interfaces.d
    mkdir -p overlay/etc/dnsmasq.d
    mkdir -p overlay/etc/hostapd
    mkdir -p overlay/var/log/vaultusb
    mkdir -p overlay/root/.ssh
    
    # Create VaultUSB service file
    cat > overlay/etc/systemd/system/vaultusb.service << 'EOF'
[Unit]
Description=VaultUSB Secure File Storage Service
After=network.target

[Service]
Type=simple
User=vaultusb
Group=vaultusb
WorkingDirectory=/opt/vaultusb
ExecStart=/usr/bin/python3 -m uvicorn app.main:app --host 0.0.0.0 --port 8000 --log-level info
Restart=always
RestartSec=10
Environment=PYTHONPATH=/opt/vaultusb

[Install]
WantedBy=multi-user.target
EOF

    # Create USB gadget setup service
    cat > overlay/etc/systemd/system/usb-gadget.service << 'EOF'
[Unit]
Description=Setup USB Ethernet Gadget
DefaultDependencies=false
After=local-fs.target
Before=network.target

[Service]
Type=oneshot
ExecStart=/opt/vaultusb/setup_usb_gadget.sh
RemainAfterExit=true

[Install]
WantedBy=multi-user.target
EOF

    # Create USB gadget setup script
    cat > overlay/opt/vaultusb/setup_usb_gadget.sh << 'EOF'
#!/bin/bash
# Setup USB Ethernet Gadget for VaultUSB

GADGET_DIR="/sys/kernel/config/usb_gadget/vaultusb"

# Load necessary modules
modprobe libcomposite
modprobe dwc2

# Create gadget directory
mkdir -p ${GADGET_DIR}
cd ${GADGET_DIR}

# Set device info
echo 0x1d6b > idVendor   # Linux Foundation
echo 0x0104 > idProduct  # Multifunction Composite Gadget
echo 0x0100 > bcdDevice  # v1.0.0
echo 0x0200 > bcdUSB     # USB 2.0

# Create English strings
mkdir -p strings/0x409
echo "VaultUSB" > strings/0x409/manufacturer
echo "VaultUSB Storage Device" > strings/0x409/product
echo "$(cat /proc/cpuinfo | grep Serial | cut -d ' ' -f 2)" > strings/0x409/serialnumber

# Create configuration
mkdir -p configs/c.1
mkdir -p configs/c.1/strings/0x409
echo "Config 1: ECM network" > configs/c.1/strings/0x409/configuration
echo 250 > configs/c.1/MaxPower

# Create ECM function
mkdir -p functions/ecm.usb0
echo "$(cat /sys/class/net/wlan0/address | sed 's/\(..\)/\1:/g; s/:$//')" > functions/ecm.usb0/dev_addr
echo "02:22:82:$(od -An -N3 -tx1 /dev/urandom | sed 's/ /:/g')" > functions/ecm.usb0/host_addr

# Link function to configuration
ln -s functions/ecm.usb0 configs/c.1/

# Enable gadget
ls /sys/class/udc > UDC

# Wait for interface
sleep 2

# Configure USB network interface
ip addr add 192.168.3.1/24 dev usb0
ip link set usb0 up

# Enable IP forwarding and NAT
echo 1 > /proc/sys/net/ipv4/ip_forward
iptables -t nat -A POSTROUTING -o wlan0 -j MASQUERADE
iptables -A FORWARD -i usb0 -o wlan0 -j ACCEPT
iptables -A FORWARD -i wlan0 -o usb0 -m state --state ESTABLISHED,RELATED -j ACCEPT
EOF

    chmod +x overlay/opt/vaultusb/setup_usb_gadget.sh
    
    # Enable services
    ln -sf ../vaultusb.service overlay/etc/systemd/system/multi-user.target.wants/
    ln -sf ../usb-gadget.service overlay/etc/systemd/system/multi-user.target.wants/
    
    log_success "Overlay structure created"
}

# Create post-build script
create_post_build_script() {
    log_info "Creating post-build script..."
    
    cat > post_build.sh << 'EOF'
#!/bin/bash
# VaultUSB Post-build script

TARGET_DIR=$1
VAULTUSB_SOURCE=$(dirname $(dirname $(realpath $0)))

echo "Installing VaultUSB Python dependencies..."

# Install Python packages
${TARGET_DIR}/usr/bin/pip3 install \
    --root=${TARGET_DIR} \
    --target=${TARGET_DIR}/usr/lib/python3.11/site-packages \
    fastapi uvicorn jinja2 python-multipart sqlmodel aiosqlite \
    pynacl argon2-cffi itsdangerous cryptography pyotp tomli \
    psutil websockets setuptools wheel

echo "Setting up VaultUSB configuration..."

# Copy configuration files
cp ${VAULTUSB_SOURCE}/config.toml ${TARGET_DIR}/opt/vaultusb/
cp ${VAULTUSB_SOURCE}/config_raspbian.toml ${TARGET_DIR}/opt/vaultusb/

# Set permissions
chown -R 1000:1000 ${TARGET_DIR}/opt/vaultusb
chmod -R 755 ${TARGET_DIR}/opt/vaultusb
chmod +x ${TARGET_DIR}/opt/vaultusb/setup_usb_gadget.sh

# Create log directory
mkdir -p ${TARGET_DIR}/var/log/vaultusb
chown 1000:1000 ${TARGET_DIR}/var/log/vaultusb

echo "VaultUSB post-build setup completed"
EOF

    chmod +x post_build.sh
    
    log_success "Post-build script created"
}

# Create custom Busybox configuration
create_busybox_config() {
    log_info "Creating custom Busybox configuration..."
    
    # Get default Busybox config and customize
    make busybox-menuconfig || true
    
    log_info "Busybox configuration created (use menuconfig to customize further)"
}

# Main build function
build_buildroot() {
    log_info "Starting Buildroot build process..."
    
    cd "../${BUILDROOT_DIR}"
    
    # Set external path
    export BR2_EXTERNAL="../br2-external-vaultusb"
    
    # Load defconfig
    make BR2_EXTERNAL="${BR2_EXTERNAL}" vaultusb_rpi0w_defconfig
    
    log_info "Configuration loaded. You can run 'make menuconfig' to customize further."
    log_info "Starting build process (this will take a while)..."
    
    # Build
    make BR2_EXTERNAL="${BR2_EXTERNAL}" -j$(nproc)
    
    log_success "Build completed!"
    log_info "Images are available in output/images/"
}

# Main function
main() {
    log_info "VaultUSB Buildroot Setup Script for Raspberry Pi Zero W"
    log_info "=================================================="
    
    check_root
    install_dependencies
    download_buildroot
    
    cd ..
    create_external_structure
    create_defconfig
    create_kernel_config
    create_config_txt
    create_overlay
    create_post_build_script
    
    log_success "Buildroot configuration completed!"
    log_info ""
    log_info "Next steps:"
    log_info "1. cd ${WORK_DIR}/${BUILDROOT_DIR}"
    log_info "2. export BR2_EXTERNAL=\"../br2-external-vaultusb\""
    log_info "3. make vaultusb_rpi0w_defconfig"
    log_info "4. make menuconfig (optional - to customize)"
    log_info "5. make -j\$(nproc)"
    log_info ""
    log_info "After building, flash output/images/sdcard.img to your SD card"
    log_info "Default login: root (no password initially)"
    log_info ""
    
    # Ask if user wants to start build immediately
    read -p "Do you want to start the build process now? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        build_buildroot
    else
        log_info "Build skipped. Run the commands above when ready."
    fi
}

# Run main function
main "$@"