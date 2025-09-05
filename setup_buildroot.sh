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
  else
    print_step "Buildroot già presente, uso versione esistente"
  fi
  
  # Copy our custom defconfig to Buildroot
  print_step "Copio defconfig personalizzato"
  echo "DEBUG: BOARD_DIR = ${BOARD_DIR}"
  echo "DEBUG: BR_DIR = ${BR_DIR}"
  echo "DEBUG: Defconfig source = ${BOARD_DIR}/configs/raspberrypi0_vaultusb_defconfig"
  echo "DEBUG: Defconfig destination = ${BR_DIR}/configs/"
  
  if [ -f "${BOARD_DIR}/configs/raspberrypi0_vaultusb_defconfig" ]; then
    echo "✓ Defconfig sorgente trovato"
    ls -la "${BOARD_DIR}/configs/raspberrypi0_vaultusb_defconfig"
  else
    echo "✗ ERRORE: Defconfig sorgente non trovato"
    echo "Contenuto di ${BOARD_DIR}/configs/:"
    ls -la "${BOARD_DIR}/configs/" || echo "Directory non esiste"
    exit 1
  fi
  
  echo "DEBUG: Copio defconfig..."
  cp "${BOARD_DIR}/configs/raspberrypi0_vaultusb_defconfig" "${BR_DIR}/configs/"
  
  # Verify defconfig was copied
  if [ -f "${BR_DIR}/configs/raspberrypi0_vaultusb_defconfig" ]; then
    echo "✓ Defconfig copiato correttamente"
    echo "Contenuto di ${BR_DIR}/configs/:"
    ls -la "${BR_DIR}/configs/" | grep vaultusb
  else
    echo "✗ ERRORE: Defconfig non copiato"
    echo "Contenuto di ${BR_DIR}/configs/:"
    ls -la "${BR_DIR}/configs/" || echo "Directory non esiste"
    exit 1
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
RESIZE_MARKER="/etc/vaultusb-fs-resized"

log() { echo "[vaultusb-firstboot] $*"; }

# Esegui auto-resize del filesystem se necessario
if [ ! -f "${RESIZE_MARKER}" ]; then
  log "Primo avvio rilevato, eseguo auto-resize filesystem..."
  
  # Attendi che la partizione sia disponibile
  for i in {1..30}; do
    if [ -b "/dev/mmcblk0p2" ]; then
      log "Partizione root trovata"
      break
    fi
    log "Attendo partizione root... (tentativo $i/30)"
    sleep 2
  done
  
  if [ -b "/dev/mmcblk0p2" ]; then
    log "Espando filesystem per utilizzare tutto lo spazio SD..."
    
    # Espandi la partizione
    parted -s /dev/mmcblk0 resizepart 2 100% || log "WARNING: Impossibile espandere partizione"
    partprobe /dev/mmcblk0
    sleep 2
    
    # Espandi il filesystem
    resize2fs /dev/mmcblk0p2 || log "WARNING: Impossibile espandere filesystem"
    
    # Crea marker
    touch "${RESIZE_MARKER}"
    log "Filesystem espanso con successo"
  else
    log "WARNING: Partizione root non trovata, skip resize"
  fi
else
  log "Filesystem già ridimensionato, skip"
fi

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

  # Crea servizio systemd per auto-resize (backup method)
  cat > "${OVERLAY_DIR}/etc/systemd/system/vaultusb-resize.service" << 'EOF'
[Unit]
Description=VaultUSB Filesystem Auto-Resize
After=local-fs.target
Before=vaultusb.service

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/vaultusb-resize.sh
RemainAfterExit=yes
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

  # Crea script di resize dedicato per systemd
  cat > "${OVERLAY_DIR}/usr/local/sbin/vaultusb-resize.sh" << 'EOF'
#!/bin/bash
# Script systemd per auto-resize filesystem

set -e

RESIZE_MARKER="/etc/vaultusb-fs-resized"
ROOT_PARTITION="/dev/mmcblk0p2"

log() {
    echo "[VaultUSB-Resize] $*" | tee -a /var/log/vaultusb-resize.log
    logger -t vaultusb-resize "$*"
}

# Controlla se il resize è già stato fatto
if [ -f "${RESIZE_MARKER}" ]; then
    log "Filesystem già ridimensionato, skip"
    exit 0
fi

log "Inizio processo di ridimensionamento filesystem..."

# Attendi che la partizione sia disponibile
for i in {1..30}; do
    if [ -b "${ROOT_PARTITION}" ]; then
        log "Partizione root trovata: ${ROOT_PARTITION}"
        break
    fi
    log "Attendo partizione root... (tentativo $i/30)"
    sleep 2
done

if [ ! -b "${ROOT_PARTITION}" ]; then
    log "ERRORE: Partizione root non trovata: ${ROOT_PARTITION}"
    exit 1
fi

# Espandi la partizione per utilizzare tutto lo spazio disponibile
log "Espando partizione per utilizzare tutto lo spazio SD..."
parted -s /dev/mmcblk0 resizepart 2 100% || {
    log "ERRORE: Impossibile espandere partizione"
    exit 1
}

# Rileva la nuova dimensione della partizione
log "Rilevo nuova dimensione partizione..."
partprobe /dev/mmcblk0
sleep 2

# Espandi il filesystem per utilizzare la nuova dimensione della partizione
log "Espando filesystem ext4..."
resize2fs "${ROOT_PARTITION}" || {
    log "ERRORE: Impossibile espandere filesystem"
    exit 1
}

# Verifica il resize
NEW_SIZE=$(df -h / | tail -1 | awk '{print $2}')
log "Filesystem ridimensionato con successo. Nuova dimensione: ${NEW_SIZE}"

# Crea marker per evitare resize futuri
touch "${RESIZE_MARKER}"
log "Marker creato: ${RESIZE_MARKER}"

log "Ridimensionamento completato con successo!"
EOF
  chmod +x "${OVERLAY_DIR}/usr/local/sbin/vaultusb-resize.sh"

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

# Use Raspberry Pi firmware instead of custom bootloader
BR2_TARGET_RPI_FIRMWARE=y
BR2_TARGET_RPI_FIRMWARE_BOOTCODE_BIN=y
BR2_TARGET_RPI_FIRMWARE_VARIANT_PI_ZERO_W=y

# Linux kernel for Raspberry Pi - use bcm2835 for Pi Zero
BR2_LINUX_KERNEL=y
BR2_LINUX_KERNEL_DEFCONFIG="bcm2835"
BR2_LINUX_KERNEL_DTS_SUPPORT=y
BR2_LINUX_KERNEL_INTREE_DTS_NAME="broadcom/bcm2835-rpi-zero"
BR2_LINUX_KERNEL_NEEDS_HOST_OPENSSL=y
BR2_LINUX_KERNEL_NEEDS_HOST_LIBELF=y

# Filesystem configuration
BR2_TARGET_ROOTFS_EXT2=y
BR2_TARGET_ROOTFS_EXT2_4=y
BR2_TARGET_ROOTFS_EXT2_SIZE="120M"
BR2_TARGET_ROOTFS_EXT2_BLOCKS=0
BR2_TARGET_ROOTFS_EXT2_INODES=0
BR2_TARGET_ROOTFS_EXT2_RESBLKS=0
BR2_TARGET_ROOTFS_EXT2_LABEL="rootfs"

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

# Filesystem resize utilities
BR2_PACKAGE_E2FSPROGS=y
BR2_PACKAGE_E2FSPROGS_RESIZE2FS=y
BR2_PACKAGE_PARTED=y
BR2_PACKAGE_FDISK=y

# Rootfs overlay
BR2_ROOTFS_OVERLAY="${OVERLAY_DIR}"

# Enable VaultUSB C++ package
BR2_PACKAGE_VAULTUSB_CPP=y

# Enable systemd service at boot
BR2_SYSTEM_DHCP="eth0"

# Post-build script disabled - using BR2_ROOTFS_GENIMAGE instead

# Host tools needed for image creation
BR2_PACKAGE_HOST_GENIMAGE=y
BR2_PACKAGE_HOST_DOSFSTOOLS=y
BR2_PACKAGE_HOST_MTOOLS=y
BR2_PACKAGE_HOST_PARTED=y

# Generate SD card image with genimage
BR2_ROOTFS_GENIMAGE=y
BR2_ROOTFS_GENIMAGE_CONFIG_FILE="$(BR2_EXTERNAL_VAULTUSB_PATH)/board/vaultusb/patches/genimage.cfg"
EOF

  # Prepare Buildroot config and build
  cd "${BR_DIR}"

  print_step "Applico defconfig personalizzato VaultUSB"
  echo "DEBUG: Directory corrente: $(pwd)"
  echo "DEBUG: Defconfig disponibili:"
  ls -la configs/ | grep vaultusb || echo "Nessun defconfig vaultusb trovato"
  echo "DEBUG: Applico defconfig..."
  make raspberrypi0_vaultusb_defconfig
  echo "DEBUG: Defconfig applicato, verifico configurazione systemd:"
  grep -i "BR2_INIT_SYSTEMD" .config || echo "BR2_INIT_SYSTEMD non trovato"
  grep -i "BR2_INIT_BUSYBOX" .config || echo "BR2_INIT_BUSYBOX non trovato"
  
  # Register external tree
  print_step "Configuro external tree"
  printf "BR2_EXTERNAL=%s\n" "${BOARD_DIR}" >> .config

  # Ensure RPI firmware configuration is present
  print_step "Verifico configurazione RPI firmware"
  if ! grep -q "BR2_TARGET_RPI_FIRMWARE=y" .config; then
    echo "Aggiungo configurazione RPI firmware manualmente..."
    echo "BR2_TARGET_RPI_FIRMWARE=y" >> .config
    echo "BR2_TARGET_RPI_FIRMWARE_BOOTCODE_BIN=y" >> .config
    echo "BR2_TARGET_RPI_FIRMWARE_VARIANT_PI_ZERO_W=y" >> .config
    echo "BR2_TARGET_RPI_FIRMWARE_CONFIG_FILE=\"/app/usb-vault/board/vaultusb/patches/config.txt\"" >> .config
    echo "BR2_TARGET_RPI_FIRMWARE_CMDLINE_FILE=\"/app/usb-vault/board/vaultusb/patches/cmdline.txt\"" >> .config
    echo "✓ Configurazione RPI firmware aggiunta"
  else
    echo "✓ Configurazione RPI firmware già presente"
  fi

  # Apply configuration
  make olddefconfig

  # Note: Non facciamo make clean per permettere build incrementali
  # Se necessario, eseguire manualmente: make clean
  
  print_step "Avvio build immagine (questo richiede tempo)"
  make -j"$(nproc)" || make

  # Execute post-image script to create SD card image
  print_step "Eseguo post-image script per creare immagine SD"
  if [ -f "${BOARD_DIR}/patches/post-image.sh" ]; then
    chmod +x "${BOARD_DIR}/patches/post-image.sh"
    "${BOARD_DIR}/patches/post-image.sh"
    print_step "Post-image script completato"
  else
    echo "WARNING: post-image.sh non trovato in ${BOARD_DIR}/patches/"
  fi

  IMG_PATH="${OUTPUT_DIR}/images/sdcard.img"
  if [ -f "${IMG_PATH}" ]; then
    print_step "Immagine SD generata: ${IMG_PATH}"
    ls -lh "${IMG_PATH}"
  else
    echo "Build completata ma immagine SD non trovata in ${IMG_PATH}. Controllare output/images/." >&2
    echo "File disponibili in output/images/:"
    ls -la "${OUTPUT_DIR}/images/" || echo "Directory images non esiste"
  fi
}

main "$@"

