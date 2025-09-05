#!/usr/bin/env bash

set -euo pipefail

# Clone the minimal buildroot project for Pi Zero
MINIMAL_REPO="https://github.com/Kytech/rpi-zero-minimal-buildroot.git"
WORKDIR="${PWD}"
MINIMAL_DIR="${WORKDIR}/rpi-zero-minimal-buildroot"

print_step() {
  echo "[+] $1"
}

main() {
  print_step "Cloning minimal buildroot project for Pi Zero"
  
  if [ ! -d "${MINIMAL_DIR}" ]; then
    git clone --recursive "${MINIMAL_REPO}" "${MINIMAL_DIR}"
  else
    print_step "Minimal buildroot project already exists, updating..."
    cd "${MINIMAL_DIR}"
    git pull
    git submodule update --init --recursive
  fi

  print_step "Setting up build environment"
  cd "${MINIMAL_DIR}/build_workdir"
  
  # Configure the external tree
  ./configure
  
  print_step "Building minimal Pi Zero image"
  make -j"$(nproc)" || make
  
  print_step "Build completed!"
  
  # Check if image was created
  if [ -f "output/images/sdcard.img" ]; then
    print_step "SD card image created: output/images/sdcard.img"
    ls -lh "output/images/sdcard.img"
  else
    echo "Build completed but sdcard.img not found. Checking available images:"
    ls -la "output/images/"
  fi
}

main "$@"
