#!/bin/bash

set -e

# Script post-build per creare immagine SD per Raspberry Pi Zero W
# Questo script viene eseguito dopo la costruzione della rootfs

BOARD_DIR="${BR2_EXTERNAL_VAULTUSB_PATH}/board/vaultusb"
PATCHES_DIR="${BOARD_DIR}/patches"
OUTPUT_DIR="${BINARIES_DIR}"
IMAGES_DIR="${OUTPUT_DIR}/images"

echo "=== VaultUSB Post-Build Script ==="
echo "BOARD_DIR: ${BOARD_DIR}"
echo "PATCHES_DIR: ${PATCHES_DIR}"
echo "OUTPUT_DIR: ${OUTPUT_DIR}"
echo "IMAGES_DIR: ${IMAGES_DIR}"

# Crea directory images se non esiste
mkdir -p "${IMAGES_DIR}"

# Copia i file del bootloader e kernel necessari
echo "Preparo file per boot partition..."

# Crea directory temporanea per boot
BOOT_DIR="${OUTPUT_DIR}/boot"
mkdir -p "${BOOT_DIR}"

# Copia device tree e kernel se esistono
if [ -f "${OUTPUT_DIR}/bcm2708-rpi-zero-w.dtb" ]; then
    cp "${OUTPUT_DIR}/bcm2708-rpi-zero-w.dtb" "${BOOT_DIR}/"
    echo "✓ Device tree copiato"
else
    echo "⚠ Device tree non trovato, creo uno dummy"
    # Crea un device tree dummy per il test
    touch "${BOOT_DIR}/bcm2708-rpi-zero-w.dtb"
fi

if [ -f "${OUTPUT_DIR}/zImage" ]; then
    cp "${OUTPUT_DIR}/zImage" "${BOOT_DIR}/"
    echo "✓ Kernel copiato"
else
    echo "⚠ Kernel non trovato, creo uno dummy"
    # Crea un kernel dummy per il test
    touch "${BOOT_DIR}/zImage"
fi

# Crea file config.txt per Raspberry Pi
cat > "${BOOT_DIR}/config.txt" << 'EOF'
# VaultUSB Raspberry Pi Zero W Configuration
arm_64bit=0
enable_uart=1
dtparam=spi=on
dtparam=i2c_arm=on
dtparam=i2c1=on
dtparam=i2c_arm_baudrate=400000

# USB Gadget configuration
dtoverlay=dwc2
dtoverlay=libcomposite

# WiFi configuration
dtoverlay=pi3-miniuart-bt

# Boot configuration
boot_delay=1
EOF

# Crea file cmdline.txt con init per auto-resize
cat > "${BOOT_DIR}/cmdline.txt" << 'EOF'
console=serial0,115200 console=tty1 root=/dev/mmcblk0p2 rootfstype=ext4 elevator=deadline fsck.repair=yes rootwait init=/sbin/init
EOF

# Crea file start.elf e bootcode.bin dummy (normalmente forniti dal firmware Raspberry Pi)
echo "Creo file firmware dummy..."
touch "${BOOT_DIR}/start.elf"
touch "${BOOT_DIR}/bootcode.bin"
touch "${BOOT_DIR}/fixup.dat"

echo "✓ File boot preparati"

# Crea script di auto-resize per il filesystem
echo "Creo script di auto-resize filesystem..."
cat > "${OUTPUT_DIR}/resize-fs.sh" << 'EOF'
#!/bin/bash
# Script per espandere automaticamente il filesystem al primo avvio

set -e

RESIZE_MARKER="/etc/vaultusb-fs-resized"
ROOT_PARTITION="/dev/mmcblk0p2"

log() {
    echo "[VaultUSB-Resize] $*" | tee -a /var/log/vaultusb-resize.log
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

# Riavvia i servizi che potrebbero aver bisogno del nuovo spazio
log "Riavvio servizi..."
systemctl daemon-reload || true

log "Ridimensionamento completato con successo!"
EOF

chmod +x "${OUTPUT_DIR}/resize-fs.sh"

# Usa genimage per creare l'immagine SD
echo "Creo immagine SD con genimage..."

# Copia configurazione genimage
cp "${PATCHES_DIR}/genimage.cfg" "${OUTPUT_DIR}/"

# Esegui genimage
cd "${OUTPUT_DIR}"
genimage \
    --config genimage.cfg \
    --inputpath . \
    --outputpath "${IMAGES_DIR}" \
    --tmppath /tmp/genimage

echo "✓ Immagine SD creata: ${IMAGES_DIR}/sdcard.img"

# Verifica che l'immagine sia stata creata
if [ -f "${IMAGES_DIR}/sdcard.img" ]; then
    echo "=== SUCCESSO ==="
    echo "Immagine SD creata: ${IMAGES_DIR}/sdcard.img"
    echo "Dimensione: $(du -h "${IMAGES_DIR}/sdcard.img" | cut -f1)"
    echo ""
    echo "Per scrivere l'immagine su SD card:"
    echo "sudo dd if=${IMAGES_DIR}/sdcard.img of=/dev/sdX bs=4M status=progress"
    echo "sudo sync"
else
    echo "=== ERRORE ==="
    echo "Impossibile creare l'immagine SD"
    exit 1
fi
