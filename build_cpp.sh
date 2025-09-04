#!/bin/bash

set -euo pipefail

echo "Building VaultUSB C++ server..."

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake -DCMAKE_BUILD_TYPE=Release ../cpp

# Build
make -j$(nproc)

echo "Build completed successfully!"
echo "Binary: build/vaultusb_cpp"
echo "To run: ./build/vaultusb_cpp --port 8000 --config ../config.toml"
