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
  # The actual C++ binary will be built inside Buildroot via a package; here we only stage assets
  if [ -d "${WORKDIR}/app/templates" ]; then
    cp -r "${WORKDIR}/app/templates" "${OVERLAY_DIR}/opt/vaultusb/" 2>/dev/null || true
  fi
  if [ -d "${WORKDIR}/app/static" ]; then
    cp -r "${WORKDIR}/app/static" "${OVERLAY_DIR}/opt/vaultusb/" 2>/dev/null || true
  fi

  # Create init scripts instead of systemd services
  mkdir -p "${OVERLAY_DIR}/etc/init.d"
  
  # VaultUSB init script
  cat > "${OVERLAY_DIR}/etc/init.d/S99vaultusb" << 'EOF'
#!/bin/sh
case "$1" in
  start)
    echo "Starting VaultUSB..."
    /usr/local/sbin/vaultusb-firstboot.sh
    start-stop-daemon --start --quiet --pidfile /var/run/vaultusb.pid --make-pidfile --background --exec /usr/bin/vaultusb_cpp -- 8000
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
if [ ! -x "/usr/bin/vaultusb_cpp" ]; then
  log "vaultusb_cpp non trovato, verificare pacchetto Buildroot"
fi

# Ensure user exists
if ! id -u vaultusb >/dev/null 2>&1; then
  useradd -r -s /usr/sbin/nologin -d /opt/vaultusb vaultusb || true
  chown -R vaultusb:vaultusb "${APP_DIR}"
fi

# Enable services (init scripts are already enabled by default)
echo "VaultUSB services will start automatically on boot"
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
config BR2_PACKAGE_VAULTUSB
    bool "vaultusb overlay"
    help
      Installa l'overlay di VaultUSB nel rootfs.
EOF

  cat > "${BOARD_DIR}/external.desc" << 'EOF'
name: VAULTUSB
desc: External BR tree for VaultUSB overlay and config
EOF

  cat > "${BOARD_DIR}/external.mk" << 'EOF'
# Hook to include our package directory
EOF

  # Create defconfig fragment enabling overlay and selecting our package
  mkdir -p "${BOARD_DIR}/configs"
  cat > "${BOARD_DIR}/configs/raspberrypi0_vaultusb_defconfig" << EOF
BR2_arm=y
BR2_arm1176jzf_s=y

# Base defconfig for Raspberry Pi Zero W is raspberrypi0w_defconfig in newer BR, raspberrypi0_defconfig otherwise
# We'll start from raspberrypi0_defconfig in the script below and then apply these fragments via defconfig append

# Init and base packages
BR2_PACKAGE_HOSTAPD=y
BR2_PACKAGE_DNSMASQ=y
BR2_PACKAGE_IFUPDOWN_SCRIPTS=y
BR2_PACKAGE_NETIFRC=y

# Rootfs overlay
BR2_ROOTFS_OVERLAY="${OVERLAY_DIR}"

# Select our C++ package
BR2_PACKAGE_VAULTUSB_CPP=y

# Enable toolchain C++
BR2_TOOLCHAIN_BUILDROOT_CXX=y
EOF

  # Prepare Buildroot config and build
  cd "${BR_DIR}"

  print_step "Applico defconfig Raspberry Pi Zero"
  export BR2_EXTERNAL="${BOARD_DIR}"
  export BR2_EXTERNAL_VAULTUSB_PATH="${BOARD_DIR}"
  if make raspberrypi0w_defconfig >/dev/null 2>&1; then
    make raspberrypi0w_defconfig
  else
    make raspberrypi0_defconfig
  fi
  
  # Clean any existing udev and systemd configuration that might conflict
  print_step "Pulisco configurazioni conflittuali"
  sed -i '/BR2_PACKAGE_UDEV/d' .config
  sed -i '/BR2_PACKAGE_EUDEV/d' .config
  sed -i '/BR2_PACKAGE_SYSTEMD/d' .config
  sed -i '/BR2_INIT_SYSTEMD/d' .config
  sed -i '/BR2_PACKAGE_SYSTEMD_LOGIND/d' .config

  # Register external tree and append our fragment
  print_step "Configuro external tree e overlay"
  # BR2_EXTERNAL already exported; ensure path var for includes
  export BR2_EXTERNAL_VAULTUSB_PATH="${BOARD_DIR}"

  # Append our fragment values to .config directly
  print_step "Aggiungo configurazioni VaultUSB"
  cat "${BOARD_DIR}/configs/raspberrypi0_vaultusb_defconfig" >> .config

  # Refresh Config to avoid stale options
  make olddefconfig

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

