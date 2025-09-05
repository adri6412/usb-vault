#!/usr/bin/env bash

set -euo pipefail

# Clone the minimal buildroot project for Pi Zero
MINIMAL_REPO="https://github.com/Kytech/rpi-zero-minimal-buildroot.git"
WORKDIR="${PWD}"
MINIMAL_DIR="${WORKDIR}/rpi-zero-minimal-buildroot"
BUILD_DIR="${MINIMAL_DIR}/build_workdir"

print_step() {
  echo "[+] $1"
}

main() {
  print_step "Setting up VaultUSB with minimal buildroot"
  
  # Clone the minimal buildroot project
  if [ ! -d "${MINIMAL_DIR}" ]; then
    git clone --recursive "${MINIMAL_REPO}" "${MINIMAL_DIR}"
  else
    print_step "Minimal buildroot project already exists, updating..."
    cd "${MINIMAL_DIR}"
    git pull
    git submodule update --init --recursive
  fi

  print_step "Setting up build environment"
  cd "${BUILD_DIR}"
  ./configure

  print_step "Customizing buildroot configuration for VaultUSB"
  
  # Enable Python3 and required packages
  echo "BR2_PACKAGE_PYTHON3=y" >> .config
  echo "BR2_PACKAGE_PYTHON3_PY_PIP=y" >> .config
  echo "BR2_PACKAGE_PYTHON3_SSL=y" >> .config
  echo "BR2_PACKAGE_PYTHON3_SQLITE3=y" >> .config
  
  # Enable networking (needed for VaultUSB)
  echo "BR2_PACKAGE_WIRELESS_TOOLS=y" >> .config
  echo "BR2_PACKAGE_WPA_SUPPLICANT=y" >> .config
  echo "BR2_PACKAGE_DHCPCD=y" >> .config
  
  # Enable USB gadget support
  echo "BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES=\"\$(BR2_EXTERNAL_RPI_ZERO_MIN_TREE_PATH)/board/raspberrypi0/linux.config\"" >> .config
  
  # Create VaultUSB package
  mkdir -p "package/vaultusb"
  
  cat > "package/vaultusb/Config.in" << 'EOF'
config BR2_PACKAGE_VAULTUSB
	bool "vaultusb"
	depends on BR2_PACKAGE_PYTHON3
	help
	  VaultUSB - Secure USB storage device
EOF

  cat > "package/vaultusb/vaultusb.mk" << 'EOF'
VAULTUSB_VERSION = 1.0
VAULTUSB_SITE = $(WORKDIR)/vaultusb
VAULTUSB_SITE_METHOD = local
VAULTUSB_DEPENDENCIES = python3

define VAULTUSB_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/app/main.py $(TARGET_DIR)/usr/local/bin/vaultusb
	$(INSTALL) -D -m 0644 $(@D)/config.toml $(TARGET_DIR)/opt/vaultusb/config.toml
	$(INSTALL) -D -m 0755 $(@D)/app/auth.py $(TARGET_DIR)/opt/vaultusb/auth.py
	$(INSTALL) -D -m 0755 $(@D)/app/crypto.py $(TARGET_DIR)/opt/vaultusb/crypto.py
	$(INSTALL) -D -m 0755 $(@D)/app/db.py $(TARGET_DIR)/opt/vaultusb/db.py
	$(INSTALL) -D -m 0755 $(@D)/app/models.py $(TARGET_DIR)/opt/vaultusb/models.py
	$(INSTALL) -D -m 0755 $(@D)/app/schemas.py $(TARGET_DIR)/opt/vaultusb/schemas.py
	$(INSTALL) -D -m 0755 $(@D)/app/storage.py $(TARGET_DIR)/opt/vaultusb/storage.py
	$(INSTALL) -D -m 0755 $(@D)/app/system.py $(TARGET_DIR)/opt/vaultusb/system.py
	$(INSTALL) -D -m 0755 $(@D)/app/wifi.py $(TARGET_DIR)/opt/vaultusb/wifi.py
	$(INSTALL) -D -m 0755 $(@D)/app/wifi_helper.py $(TARGET_DIR)/opt/vaultusb/wifi_helper.py
	$(INSTALL) -D -m 0755 $(@D)/app/cpp/vaultusb_cpp $(TARGET_DIR)/usr/local/bin/vaultusb_cpp
	cp -r $(@D)/app/templates $(TARGET_DIR)/opt/vaultusb/
	cp -r $(@D)/app/static $(TARGET_DIR)/opt/vaultusb/
endef

$(eval $(generic-package))
EOF

  # Create VaultUSB init script
  mkdir -p "overlay/etc/init.d"
  cat > "overlay/etc/init.d/S99vaultusb" << 'EOF'
#!/bin/sh
case "$1" in
  start)
    echo "Starting VaultUSB..."
    cd /opt/vaultusb
    /usr/local/bin/vaultusb --port 8000 --config config.toml &
    ;;
  stop)
    echo "Stopping VaultUSB..."
    pkill -f "vaultusb"
    ;;
  restart)
    $0 stop
    $0 start
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1
    ;;
esac
exit 0
EOF
  chmod +x "overlay/etc/init.d/S99vaultusb"

  # Create VaultUSB directory
  mkdir -p "overlay/opt/vaultusb"
  mkdir -p "vaultusb"

  # Copy VaultUSB files to build directory
  if [ -d "${WORKDIR}/app" ]; then
    cp -r "${WORKDIR}/app" "vaultusb/"
  fi
  if [ -f "${WORKDIR}/config.toml" ]; then
    cp "${WORKDIR}/config.toml" "vaultusb/"
  fi

  # Update package list
  echo "source \"package/vaultusb/Config.in\"" >> package/Config.in

  # Apply configuration
  make olddefconfig

  print_step "Building VaultUSB image"
  make -j"$(nproc)" || make

  print_step "Build completed!"
  
  if [ -f "output/images/sdcard.img" ]; then
    print_step "VaultUSB SD card image: output/images/sdcard.img"
    ls -lh "output/images/sdcard.img"
  else
    echo "Build completed but sdcard.img not found. Checking available images:"
    ls -la "output/images/"
  fi
}

main "$@"
