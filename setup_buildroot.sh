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

  # Build PyInstaller binaries for ARM using Docker
  print_step "Compilo VaultUSB con PyInstaller per ARM usando Docker"
  if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
    # Create temporary build environment
    BUILD_DIR="${WORKDIR}/build_arm"
    mkdir -p "${BUILD_DIR}"
    
    # Copy source files
    rsync -a --exclude "third_party/" --exclude ".git/" --exclude "output/" \
      --exclude "__pycache__/" --exclude "*.pyc" --exclude ".venv/" --exclude "venv/" \
      "${WORKDIR}/" "${BUILD_DIR}/"
    
    # Create Dockerfile for ARM compilation with QEMU
    cat > "${BUILD_DIR}/Dockerfile" << 'EOF'
FROM --platform=linux/arm/v6 python:3.11-slim

WORKDIR /app

# Install build dependencies
RUN apt-get update && apt-get install -y \
    gcc \
    g++ \
    libffi-dev \
    libssl-dev \
    python3-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy requirements and install
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
RUN pip install --no-cache-dir pyinstaller

# Copy source code
COPY . .

# Build with PyInstaller
RUN pyinstaller --onefile --name vaultusb \
    --add-data "app:app" \
    --add-data "templates:templates" \
    --add-data "static:static" \
    --hidden-import uvicorn \
    --hidden-import fastapi \
    --hidden-import jinja2 \
    app/main.py

# Copy static files
RUN mkdir -p /output/opt/vaultusb
RUN cp -r templates /output/opt/vaultusb/
RUN cp -r static /output/opt/vaultusb/
RUN cp dist/vaultusb /output/usr/local/bin/vaultusb
RUN chmod +x /output/usr/local/bin/vaultusb
EOF

    # Setup QEMU for ARM emulation
    print_step "Configuro QEMU per emulazione ARM"
    docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
    
    # Build Docker image and extract binary
    cd "${BUILD_DIR}"
    mkdir -p output
    docker build --platform linux/arm/v6 -t vaultusb-arm -f Dockerfile .
    docker run --platform linux/arm/v6 --rm -v "${BUILD_DIR}/output:/output" vaultusb-arm
    
    # Copy the compiled binary to overlay
    cp output/usr/local/bin/vaultusb "${OVERLAY_DIR}/usr/local/bin/vaultusb"
    chmod +x "${OVERLAY_DIR}/usr/local/bin/vaultusb"
    
    # Copy static files and templates
    mkdir -p "${OVERLAY_DIR}/opt/vaultusb"
    cp -r output/opt/vaultusb/* "${OVERLAY_DIR}/opt/vaultusb/"
    
    cd "${WORKDIR}"
    rm -rf "${BUILD_DIR}"
  else
    print_step "Docker non trovato, copio sorgenti Python (richiede venv al runtime)"
    rsync -a --delete --exclude "third_party/" --exclude ".git/" --exclude "output/" \
      --exclude "__pycache__/" --exclude "*.pyc" --exclude ".venv/" --exclude "venv/" \
      "${WORKDIR}/" "${OVERLAY_DIR}/opt/vaultusb/"
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
    start-stop-daemon --start --quiet --pidfile /var/run/vaultusb.pid --make-pidfile --background --exec /usr/local/bin/vaultusb
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

# Check if we have PyInstaller binary or need to create venv
if [ -f "/usr/local/bin/vaultusb" ]; then
  log "Using PyInstaller binary - no venv needed"
else
  log "Creating Python venv and installing requirements"
  VENV_DIR="${APP_DIR}/venv"
  if [ ! -d "${VENV_DIR}" ]; then
    python3 -m venv "${VENV_DIR}"
    "${VENV_DIR}/bin/pip" install --upgrade pip wheel setuptools
    if [ -f "${APP_DIR}/requirements.txt" ]; then
      "${VENV_DIR}/bin/pip" install -r "${APP_DIR}/requirements.txt"
    fi
  fi
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
# Empty: overlay handled via BR2_ROOTFS_OVERLAY in defconfig fragment
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

# Python runtime (minimal for PyInstaller binaries)
BR2_PACKAGE_PYTHON3=y
BR2_PACKAGE_PYTHON3_SSL=y

# Rootfs overlay
BR2_ROOTFS_OVERLAY="${OVERLAY_DIR}"

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
  
  # Clean any existing udev and systemd configuration that might conflict
  print_step "Pulisco configurazioni conflittuali"
  sed -i '/BR2_PACKAGE_UDEV/d' .config
  sed -i '/BR2_PACKAGE_EUDEV/d' .config
  sed -i '/BR2_PACKAGE_SYSTEMD/d' .config
  sed -i '/BR2_INIT_SYSTEMD/d' .config
  sed -i '/BR2_PACKAGE_SYSTEMD_LOGIND/d' .config

  # Register external tree and append our fragment
  print_step "Configuro external tree e overlay"
  printf "BR2_EXTERNAL=%s\n" "${BOARD_DIR}" >> .config

  # Append our fragment values to .config directly
  print_step "Aggiungo configurazioni VaultUSB"
  cat "${BOARD_DIR}/configs/raspberrypi0_vaultusb_defconfig" >> .config

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

