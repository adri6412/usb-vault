#!/bin/bash
set -e

echo "=== VaultUSB Post-Build Script ==="
echo "BOARD_DIR: $1"
echo "PATCHES_DIR: $1/patches"
echo "OUTPUT_DIR: $2"
echo "IMAGES_DIR: $2/images"

BOARD_DIR="$1"
PATCHES_DIR="$1/patches"
OUTPUT_DIR="$2"
IMAGES_DIR="$2/images"

# Percorsi corretti
TARGET_DIR="/app/usb-vault/third_party/buildroot-2024.02.6/output/target"
IMAGES_OUTPUT_DIR="/app/usb-vault/third_party/buildroot-2024.02.6/output/images"

# Crea le immagini se non esistono
if [ ! -f "$IMAGES_OUTPUT_DIR/rootfs.ext2" ]; then
    echo "Creo immagine rootfs.ext2..."
    cd "$IMAGES_OUTPUT_DIR"
    dd if=/dev/zero of=rootfs.ext2 bs=1M count=120
    mkfs.ext2 -F rootfs.ext2
    mkdir -p /tmp/rootfs_mount
    mount -o loop rootfs.ext2 /tmp/rootfs_mount
    cp -a "$TARGET_DIR"/* /tmp/rootfs_mount/
    umount /tmp/rootfs_mount || umount -f /tmp/rootfs_mount
    rmdir /tmp/rootfs_mount
    echo "✓ Immagine rootfs.ext2 creata"
fi

if [ ! -f "$IMAGES_OUTPUT_DIR/boot.vfat" ]; then
    echo "Creo immagine boot.vfat..."
    cd "$IMAGES_OUTPUT_DIR"
    dd if=/dev/zero of=boot.vfat bs=1M count=256
    mkfs.vfat -F 32 boot.vfat
    mkdir -p /tmp/boot_mount
    mount -o loop boot.vfat /tmp/boot_mount
    cp -a "$IMAGES_OUTPUT_DIR/boot"/* /tmp/boot_mount/
    umount /tmp/boot_mount || umount -f /tmp/boot_mount
    rmdir /tmp/boot_mount
    echo "✓ Immagine boot.vfat creata"
fi

# Crea genimage.cfg corretto per Raspberry Pi
cat > "$PATCHES_DIR/genimage.cfg" << 'GENEOF'
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
        image = "rootfs.ext2"
        size = 120M
    }
}
GENEOF

# Crea l'immagine SD
echo "Creo immagine SD con genimage..."
cd "$IMAGES_OUTPUT_DIR"
mkdir -p input
cp boot.vfat input/
cp rootfs.ext2 input/
/app/usb-vault/third_party/buildroot-2024.02.6/output/host/bin/genimage --config "$PATCHES_DIR/genimage.cfg" --rootpath "$TARGET_DIR"

echo "✓ Post-build script completato"
echo "✓ Immagine SD creata: $IMAGES_OUTPUT_DIR/sdcard.img"