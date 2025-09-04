#!/usr/bin/env bash

set -euo pipefail

BR_VERSION="2024.02.6"
BR_TARBALL="buildroot-${BR_VERSION}.tar.gz"
BR_URL="https://buildroot.org/downloads/${BR_TARBALL}"
WORKDIR="${PWD}"
BR_DIR="${WORKDIR}/third_party/buildroot-${BR_VERSION}"
BOARD_DIR="${WORKDIR}/board/vaultusb"
OVERLAY_DIR="${BOARD_DIR}/overlay"
OUTPUT_DIR="${BR_DIR}/output"

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "Error: required command '$1' not found" >&2; exit 1; }
}

print_step() {
  echo "[+] $1"
}

main() {
  require_cmd curl
  require_cmd tar
  require_cmd sha256sum || require_cmd shasum || true
  require_cmd git || true

  mkdir -p "${WORKDIR}/third_party"
  mkdir -p "${BOARD_DIR}/rootfs_overlay"

  if [ ! -d "${BR_DIR}" ]; then
    print_step "Scarico Buildroot ${BR_VERSION}"
    cd "${WORKDIR}/third_party"
    if [ ! -f "${BR_TARBALL}" ]; then
      curl -fL "${BR_URL}" -o "${BR_TARBALL}"
    fi
    tar xf "${BR_TARBALL}"
  fi

  # Prepare overlay structure
  mkdir -p "${OVERLAY_DIR}/opt/vaultusb"
  mkdir -p "${OVERLAY_DIR}/etc/systemd/system"
  mkdir -p "${OVERLAY_DIR}/etc/dnsmasq.d"
  mkdir -p "${OVERLAY_DIR}/etc/hostapd"
  mkdir -p "${OVERLAY_DIR}/usr/local/sbin"
  mkdir -p "${OVERLAY_DIR}/boot"

  # Build C++ server (cross-compiled by Buildroot toolchain at BR time)
  print_step "Preparo overlay per server C++"
  mkdir -p "${OVERLAY_DIR}/usr/local/bin"
  mkdir -p "${OVERLAY_DIR}/opt/vaultusb"
  
  # Copy templates and static files
  if [ -d "${WORKDIR}/app/templates" ]; then
    cp -r "${WORKDIR}/app/templates" "${OVERLAY_DIR}/opt/vaultusb/" 2>/dev/null || true
  fi
  if [ -d "${WORKDIR}/app/static" ]; then
    cp -r "${WORKDIR}/app/static" "${OVERLAY_DIR}/opt/vaultusb/" 2>/dev/null || true
  fi
  
  # Copy configuration files
  if [ -f "${WORKDIR}/config.toml" ]; then
    cp "${WORKDIR}/config.toml" "${OVERLAY_DIR}/opt/vaultusb/" 2>/dev/null || true
  fi

  # Create init scripts instead of systemd services
  mkdir -p "${OVERLAY_DIR}/etc/init.d"
  
  # VaultUSB init script
  cat > "${OVERLAY_DIR}/etc/init.d/S99vaultusb" << 'EOF'
#!/bin/sh
case "$1" in
  start)
    echo "Starting VaultUSB C++ server..."
    /usr/local/sbin/vaultusb-firstboot.sh
    start-stop-daemon --start --quiet --pidfile /var/run/vaultusb.pid --make-pidfile --background --exec /usr/local/bin/vaultusb_cpp -- --port 8000 --config /opt/vaultusb/config.toml
    ;;
  stop)
    echo "Stopping VaultUSB..."
    start-stop-daemon --stop --quiet --pidfile /var/run/vaultusb.pid
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
  chmod +x "${OVERLAY_DIR}/etc/init.d/S99vaultusb"

  # Seed minimal network configs (reusing repo configs if present)
  if [ -f "${WORKDIR}/networking/dnsmasq-uap0.conf" ]; then
    cp "${WORKDIR}/networking/dnsmasq-uap0.conf" "${OVERLAY_DIR}/etc/dnsmasq.d/dnsmasq-uap0.conf"
  fi
  if [ -f "${WORKDIR}/networking/dnsmasq-usb0.conf" ]; then
    cp "${WORKDIR}/networking/dnsmasq-usb0.conf" "${OVERLAY_DIR}/etc/dnsmasq.d/dnsmasq-usb0.conf"
  fi
  if [ -f "${WORKDIR}/networking/hostapd.conf" ]; then
    cp "${WORKDIR}/networking/hostapd.conf" "${OVERLAY_DIR}/etc/hostapd/hostapd.conf"
  fi

  # First-boot provision script
  cat > "${OVERLAY_DIR}/usr/local/sbin/vaultusb-firstboot.sh" << 'EOF'
#!/usr/bin/env bash
set -euo pipefail

APP_DIR="/opt/vaultusb"

log() { echo "[vaultusb-firstboot] $*"; }

# C++ binary is shipped by Buildroot package as /usr/local/bin/vaultusb_cpp
if [ ! -x "/usr/local/bin/vaultusb_cpp" ]; then
  log "vaultusb_cpp non trovato, verificare pacchetto Buildroot"
  exit 1
fi

# Ensure user exists
if ! id -u vaultusb >/dev/null 2>&1; then
  useradd -r -s /usr/sbin/nologin -d /opt/vaultusb vaultusb || true
  chown -R vaultusb:vaultusb "${APP_DIR}"
fi

# Create vault directory
mkdir -p "${APP_DIR}/vault"
chown vaultusb:vaultusb "${APP_DIR}/vault"
chmod 700 "${APP_DIR}/vault"

# Set up database directory
mkdir -p "$(dirname "${APP_DIR}/vault.db")"
chown vaultusb:vaultusb "$(dirname "${APP_DIR}/vault.db")"

# Enable services (init scripts are already enabled by default)
echo "VaultUSB C++ services will start automatically on boot"
EOF
  chmod +x "${OVERLAY_DIR}/usr/local/sbin/vaultusb-firstboot.sh"

  # Init script to run first-boot provisioning
  cat > "${OVERLAY_DIR}/etc/init.d/S98vaultusb-firstboot" << 'EOF'
#!/bin/sh
case "$1" in
  start)
    echo "Running VaultUSB first-boot provisioning..."
    /usr/local/sbin/vaultusb-firstboot.sh
    ;;
  stop)
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
  chmod +x "${OVERLAY_DIR}/etc/init.d/S98vaultusb-firstboot"

  # Create BR external tree to register overlay and options
  mkdir -p "${BOARD_DIR}"
  cat > "${BOARD_DIR}/Config.in" << 'EOF'
source "$BR2_EXTERNAL_VAULTUSB_PATH/package/vaultusb-cpp/Config.in"
EOF

  cat > "${BOARD_DIR}/external.desc" << 'EOF'
name: VAULTUSB
desc: External BR tree for VaultUSB C++ application and config
EOF

  cat > "${BOARD_DIR}/external.mk" << 'EOF'
# Hook to include our package directory
EOF

  # Create defconfig fragment enabling systemd and overlay
  mkdir -p "${BOARD_DIR}/configs"
  cat > "${BOARD_DIR}/configs/raspberrypi0_vaultusb_defconfig" << EOF
BR2_arm=y
BR2_cortex_a7=y
BR2_ARM_FPU_VFPV3D16=y
BR2_ARM_EABIHF=y

# Base defconfig for Raspberry Pi Zero W is raspberrypi0w_defconfig in newer BR, raspberrypi0_defconfig otherwise
# We'll start from raspberrypi0_defconfig in the script below and then apply these fragments via defconfig append

# Use BusyBox init instead of systemd (simpler for embedded)
BR2_INIT_BUSYBOX=y
BR2_PACKAGE_BUSYBOX=y
BR2_PACKAGE_HOSTAPD=y
BR2_PACKAGE_DNSMASQ=y
BR2_PACKAGE_IFUPDOWN_SCRIPTS=y
BR2_PACKAGE_NETIFRC=y

# Disable systemd completely
BR2_INIT_SYSTEMD=n
BR2_PACKAGE_SYSTEMD=n

# C++ runtime and libraries
BR2_TOOLCHAIN_BUILDROOT_CXX=y
BR2_PACKAGE_LIBSTDCPP=y

# SQLite3 for database
BR2_PACKAGE_SQLITE=y

# OpenSSL for crypto operations
BR2_PACKAGE_OPENSSL=y
BR2_PACKAGE_OPENSSL_BIN=y

# Argon2 for password hashing
BR2_PACKAGE_LIBARGON2=y

# Additional utilities
BR2_PACKAGE_UTIL_LINUX=y
BR2_PACKAGE_UTIL_LINUX_LIBUUID=y
BR2_PACKAGE_UTIL_LINUX_LIBBLKID=y

# Rootfs overlay
BR2_ROOTFS_OVERLAY="${OVERLAY_DIR}"

# Enable VaultUSB C++ package
BR2_PACKAGE_VAULTUSB_CPP=y

# Enable systemd service at boot
BR2_SYSTEM_DHCP="eth0"
EOF

  # Prepare Buildroot config and build
  cd "${BR_DIR}"

  print_step "Applico defconfig Raspberry Pi Zero"
  if make raspberrypi0w_defconfig >/dev/null 2>&1; then
    make raspberrypi0w_defconfig
  else
    make raspberrypi0_defconfig
  fi
  
  # Clean any existing udev and init system configuration that might conflict
  print_step "Pulisco configurazioni conflittuali"
  sed -i '/BR2_PACKAGE_UDEV/d' .config
  sed -i '/BR2_PACKAGE_EUDEV/d' .config
  sed -i '/BR2_INIT_BUSYBOX/d' .config
  sed -i '/BR2_SYSTEM_BIN_SH_BUSYBOX/d' .config
  sed -i '/BR2_PACKAGE_BUSYBOX/d' .config

  # Register external tree and append our fragment
  print_step "Configuro external tree e overlay"
  printf "BR2_EXTERNAL=%s\n" "${BOARD_DIR}" >> .config

  # Force systemd as init system
  print_step "Forzo systemd come init system"
  echo "BR2_INIT_SYSTEMD=y" >> .config
  echo "BR2_INIT_BUSYBOX=n" >> .config
  echo "BR2_PACKAGE_SYSTEMD=y" >> .config
  echo "BR2_PACKAGE_SYSTEMD_UTILS=y" >> .config
  echo "BR2_PACKAGE_SYSTEMD_NETWORKD=y" >> .config
  echo "BR2_PACKAGE_SYSTEMD_RESOLVED=y" >> .config
  echo "BR2_PACKAGE_SYSTEMD_TIMESYNCD=y" >> .config
  echo "BR2_SYSTEM_BIN_SH_BASH=y" >> .config

  # Append our fragment values to .config directly
  print_step "Aggiungo configurazioni VaultUSB"
  cat "${BOARD_DIR}/configs/raspberrypi0_vaultusb_defconfig" >> .config

  # Clean and reconfigure to avoid dependency issues
  print_step "Pulisco build precedente per evitare conflitti"
  make clean
  
  print_step "Avvio build immagine (questo richiede tempo)"
  make -j"$(nproc)" || make

  IMG_PATH="${OUTPUT_DIR}/images/sdcard.img"
  if [ -f "${IMG_PATH}" ]; then
    print_step "Immagine generata: ${IMG_PATH}"
  else
    echo "Build completata ma immagine non trovata in ${IMG_PATH}. Controllare output/images/." >&2
  fi
}

main "$@"

