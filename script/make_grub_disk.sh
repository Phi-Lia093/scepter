#!/bin/bash
#
# Create a bootable ext2 root disk image (default: root.img):
#
#   - (optional) GRUB bootloader in the MBR (i386-pc, multiboot)
#   - one ext2 partition (hda1) that serves BOTH as the boot partition
#     (GRUB reads /boot/kernel.elf from it) and as the root filesystem
#     (the kernel mounts it at /, containing /init and /bin)
#   - /dev is populated at runtime by init (devfs automount)
#
# Usage: sudo ./script/make_grub_disk.sh [DISK_IMG] [grub|nogrub]
#
#   DISK_IMG   output file name (default root.img)
#   nogrub     skip GRUB installation (used for the x86_64 root fs; the
#              bootloader lives on efi.img there)
#
# Examples:
#   sudo ./script/make_grub_disk.sh               # root.img with GRUB (i386)
#   sudo ./script/make_grub_disk.sh root64.img nogrub
#

set -e  # Exit on error

DISK_IMG="${1:-root.img}"
MODE="${2:-grub}"
DISK_SIZE_MB=128

echo "=============================================="
echo "Creating Bootable Root Disk Image ($DISK_IMG)"
echo "=============================================="

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: This script must be run as root (use sudo)"
    exit 1
fi

# Step 1: Create blank disk image
echo "[1/7] Creating ${DISK_SIZE_MB}MB disk image..."
dd if=/dev/zero of="$DISK_IMG" bs=1M count=$DISK_SIZE_MB status=progress

# Step 2: Create partition table with fdisk (one bootable Linux/ext2 partition)
echo "[2/7] Creating MBR partition table..."
fdisk "$DISK_IMG" << EOF > /dev/null 2>&1
o
n
p
1
2048

t
83
a
w
EOF

# Step 3: Setup loop device
echo "[3/7] Setting up loop device..."
LOOP_DEV=$(losetup -f)
losetup -P "$LOOP_DEV" "$DISK_IMG"
echo "    Loop device: $LOOP_DEV"

# Wait for partition to appear
sleep 1

# Step 4: Format partition as ext2 (the kernel's ext2 driver + GRUB's
# ext2 module both understand this format)
echo "[4/7] Formatting partition as ext2..."
mkfs.ext2 -F "${LOOP_DEV}p1" > /dev/null 2>&1

# Step 5: Mount partition temporarily
echo "[5/7] Mounting partition..."
TEMP_MOUNT=$(mktemp -d)
mount "${LOOP_DEV}p1" "$TEMP_MOUNT"

# Step 6: Install GRUB (i386-pc) unless the caller asked for no GRUB.
# The ext2 module is embedded in the core image so GRUB can read the
# root partition to find grub.cfg and load /boot/kernel.elf.
if [ "$MODE" = "grub" ]; then
    echo "[6/7] Installing GRUB bootloader..."
    grub-install --target=i386-pc --boot-directory="$TEMP_MOUNT/boot" --install-modules="ext2 normal multiboot" "$LOOP_DEV" 2>&1 | grep -v "Installing"
else
    echo "[6/7] Skipping GRUB installation ($MODE mode)..."
fi

# Step 7: Create grub.cfg (only in GRUB mode)
if [ "$MODE" = "grub" ]; then
    echo "[7/7] Creating GRUB configuration..."
    mkdir -p "$TEMP_MOUNT/boot/grub"
    cat > "$TEMP_MOUNT/boot/grub/grub.cfg" << 'EOF'
set timeout=5
set default=0

menuentry "kernel" {
    multiboot /boot/kernel.elf
    boot
}
EOF
fi

# Root filesystem layout: /boot (GRUB), /bin, /dev (devfs mounts over it),
# and /init is installed later by 'make app'.
mkdir -p "$TEMP_MOUNT/bin" "$TEMP_MOUNT/dev"

# Cleanup
sync
umount "$TEMP_MOUNT"
rmdir "$TEMP_MOUNT"
losetup -d "$LOOP_DEV"
chmod 666 "$DISK_IMG"

echo ""
echo "=============================================="
echo "SUCCESS! Bootable root disk created: $DISK_IMG"
echo "=============================================="
echo "  Partition 1: ext2 filesystem (type 0x83, bootable)"
if [ "$MODE" = "grub" ]; then
    echo "  /boot  -> GRUB + kernel.elf"
fi
echo "  /bin   -> core utilities (installed by 'make app')"
echo "  /init  -> init process (installed by 'make app')"
echo "  /dev   -> devfs (automounted by init)"
echo ""
echo "Run 'make app' to install the userspace, then 'make run'."
echo ""