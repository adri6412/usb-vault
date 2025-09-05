#!/bin/bash
set -e

echo "=== VaultUSB Post-Image Script ==="
echo "Generating SD card image with genimage..."

# Percorsi
IMAGES_DIR="/app/usb-vault/third_party/buildroot-2024.02.6/output/images"
PATCHES_DIR="/app/usb-vault/board/vaultusb/patches"
TARGET_DIR="/app/usb-vault/third_party/buildroot-2024.02.6/output/target"

# Debug: Show available files
echo "Available files in images directory:"
ls -la "$IMAGES_DIR"

# Crea le immagini se non esistono
ROOTFS_IMAGE=""
if [ -f "$IMAGES_DIR/rootfs.ext4" ]; then
    ROOTFS_IMAGE="rootfs.ext4"
elif [ -f "$IMAGES_DIR/rootfs.ext2" ]; then
    ROOTFS_IMAGE="rootfs.ext2"
else
    echo "ERROR: No rootfs image found! Looking for rootfs.ext4 or rootfs.ext2"
    ls -la "$IMAGES_DIR"/rootfs.*
    exit 1
fi

echo "Using rootfs image: $ROOTFS_IMAGE"

# Always recreate boot.vfat to ensure it's up to date
echo "Creating boot.vfat image..."
cd "$IMAGES_DIR"

# Check if boot directory exists
if [ ! -d "$IMAGES_DIR/boot" ]; then
    echo "ERROR: boot directory not found at $IMAGES_DIR/boot"
    ls -la "$IMAGES_DIR"
    exit 1
fi

# Debug: Show boot directory contents
echo "Boot directory contents:"
ls -la "$IMAGES_DIR/boot"

# Create boot.vfat image
dd if=/dev/zero of=boot.vfat bs=1M count=256
mkfs.vfat -F 32 boot.vfat
mkdir -p /tmp/boot_mount
mount -o loop boot.vfat /tmp/boot_mount

# Copy all files from boot directory
echo "Copying files to boot.vfat..."
cp -a "$IMAGES_DIR/boot"/* /tmp/boot_mount/

# Debug: Verify files were copied
echo "Files copied to boot.vfat:"
ls -la /tmp/boot_mount/

# Check file sizes
echo "File sizes in boot.vfat:"
du -h /tmp/boot_mount/*

umount /tmp/boot_mount
rmdir /tmp/boot_mount
echo "✓ boot.vfat created"

# Crea genimage.cfg
cat > "$PATCHES_DIR/genimage.cfg" << 'EOF'
image sdcard.img {
    hdimage {
        partition-table-type = "mbr"
    }

    partition boot {
        partition-type = 0xC
        bootable = true
        image = "boot.vfat"
        size = 256M
    }

    partition rootfs {
        partition-type = 0x83
        image = "$ROOTFS_IMAGE"
        size = 120M
    }
}
EOF

# Crea l'immagine SD
echo "Creating SD card image..."
cd "$IMAGES_DIR"
mkdir -p input
cp boot.vfat input/
cp "$ROOTFS_IMAGE" input/
/app/usb-vault/third_party/buildroot-2024.02.6/output/host/bin/genimage --config "$PATCHES_DIR/genimage.cfg" --rootpath "$TARGET_DIR"

echo "✓ SD card image created: $IMAGES_DIR/sdcard.img"
