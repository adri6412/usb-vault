#!/usr/bin/env bash

set -euo pipefail

WORKDIR="${PWD}"
MINIMAL_DIR="${WORKDIR}/rpi-zero-minimal-buildroot"
BUILD_DIR="${MINIMAL_DIR}/build_workdir"

print_step() {
  echo "[+] $1"
}

main() {
  if [ ! -d "${MINIMAL_DIR}" ]; then
    echo "Error: Minimal buildroot project not found. Run setup_minimal_buildroot.sh first."
    exit 1
  fi

  print_step "Customizing minimal image with VaultUSB"
  
  cd "${BUILD_DIR}"
  
  # Create overlay directory
  mkdir -p "overlay/opt/vaultusb"
  mkdir -p "overlay/etc/init.d"
  mkdir -p "overlay/usr/local/bin"
  
  # Copy VaultUSB files
  if [ -d "${WORKDIR}/app" ]; then
    print_step "Copying VaultUSB application files"
    cp -r "${WORKDIR}/app"/* "overlay/opt/vaultusb/" 2>/dev/null || true
  fi
  
  # Copy configuration
  if [ -f "${WORKDIR}/config.toml" ]; then
    cp "${WORKDIR}/config.toml" "overlay/opt/vaultusb/" 2>/dev/null || true
  fi
  
  # Create VaultUSB init script
  cat > "overlay/etc/init.d/S99vaultusb" << 'EOF'
#!/bin/sh
case "$1" in
  start)
    echo "Starting VaultUSB..."
    cd /opt/vaultusb
    python3 main.py --port 8000 --config config.toml &
    ;;
  stop)
    echo "Stopping VaultUSB..."
    pkill -f "python3 main.py"
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
  
  print_step "Rebuilding image with VaultUSB customization"
  make
  
  print_step "Customization completed!"
  
  if [ -f "output/images/sdcard.img" ]; then
    print_step "Customized SD card image: output/images/sdcard.img"
    ls -lh "output/images/sdcard.img"
  fi
}

main "$@"
