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

  # Copy application files into overlay
  print_step "Copia dei sorgenti VaultUSB nell'overlay"
  rsync -a --delete --exclude "third_party/" --exclude ".git/" --exclude "output/" \
    --exclude "__pycache__/" --exclude "*.pyc" --exclude ".venv/" --exclude "venv/" \
    "${WORKDIR}/" "${OVERLAY_DIR}/opt/vaultusb/"

  # Install systemd unit files
  if [ -f "${WORKDIR}/systemd/vaultusb.service" ]; then
    cp "${WORKDIR}/systemd/vaultusb.service" "${OVERLAY_DIR}/etc/systemd/system/vaultusb.service"
  fi
  if [ -f "${WORKDIR}/systemd/hostapd.service" ]; then
    cp "${WORKDIR}/systemd/hostapd.service" "${OVERLAY_DIR}/etc/systemd/system/hostapd.service"
  fi
  if [ -f "${WORKDIR}/systemd/dnsmasq@.service" ]; then
    cp "${WORKDIR}/systemd/dnsmasq@.service" "${OVERLAY_DIR}/etc/systemd/system/dnsmasq@.service"
  fi
  if [ -f "${WORKDIR}/systemd/uap0-setup.service" ]; then
    cp "${WORKDIR}/systemd/uap0-setup.service" "${OVERLAY_DIR}/etc/systemd/system/uap0-setup.service"
  fi

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

  # First-boot provision script to create venv and install deps
  cat > "${OVERLAY_DIR}/usr/local/sbin/vaultusb-firstboot.sh" << 'EOF'
#!/usr/bin/env bash
set -euo pipefail

APP_DIR="/opt/vaultusb"
VENV_DIR="${APP_DIR}/venv"

log() { echo "[vaultusb-firstboot] $*"; }

if [ ! -d "${VENV_DIR}" ]; then
  log "Creating Python venv and installing requirements"
  python3 -m venv "${VENV_DIR}"
  "${VENV_DIR}/bin/pip" install --upgrade pip wheel setuptools
  if [ -f "${APP_DIR}/requirements.txt" ]; then
    "${VENV_DIR}/bin/pip" install -r "${APP_DIR}/requirements.txt"
  fi
fi

# Ensure user exists
if ! id -u vaultusb >/dev/null 2>&1; then
  useradd -r -s /usr/sbin/nologin -d /opt/vaultusb vaultusb || true
  chown -R vaultusb:vaultusb "${APP_DIR}"
fi

# Enable services
systemctl enable vaultusb.service || true
systemctl enable uap0-setup.service || true
systemctl enable hostapd.service || true
systemctl enable dnsmasq@uap0.service || true

systemctl daemon-reload || true
EOF
  chmod +x "${OVERLAY_DIR}/usr/local/sbin/vaultusb-firstboot.sh"

  # Systemd unit to run first-boot provisioning
  cat > "${OVERLAY_DIR}/etc/systemd/system/vaultusb-firstboot.service" << 'EOF'
[Unit]
Description=VaultUSB First Boot Provisioning
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/vaultusb-firstboot.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

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

# Systemd
BR2_INIT_SYSTEMD=y
BR2_PACKAGE_SYSTEMD=y
BR2_PACKAGE_SYSTEMD_JOURNAL_GATEWAY=y
BR2_PACKAGE_SYSTEMD_NETWORKD=y
BR2_PACKAGE_HOSTAPD=y
BR2_PACKAGE_DNSMASQ=y

# Disable udev when using systemd (systemd includes udev)
BR2_PACKAGE_UDEV=n
BR2_PACKAGE_EUDEV=n
BR2_PACKAGE_SYSTEMD_LOGIND=y

# Python runtime
BR2_PACKAGE_PYTHON3=y
BR2_PACKAGE_PYTHON3_PIP=y
BR2_PACKAGE_PYTHON3_SSL=y
BR2_PACKAGE_PYTHON3_SETuptools=y
BR2_PACKAGE_PYTHON3_WHEEL=y

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
  
  # Clean any existing udev configuration that might conflict
  print_step "Pulisco configurazioni udev conflittuali"
  sed -i '/BR2_PACKAGE_UDEV/d' .config
  sed -i '/BR2_PACKAGE_EUDEV/d' .config

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

