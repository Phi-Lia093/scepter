#!/bin/bash
#
# Create a 128MB test disk with Minix partition
# Usage: sudo ./script/make_test_disk.sh
#

set -e  # Exit on error

DISK_IMG="data.img"
DISK_SIZE_MB=128

echo "=================================================="
echo "Creating 128MB Test Disk with Minix Partition"
echo "=================================================="

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: This script must be run as root (use sudo)"
    exit 1
fi

# Step 1: Create blank disk image
echo "[1/5] Creating ${DISK_SIZE_MB}MB disk image..."
dd if=/dev/zero of="$DISK_IMG" bs=1M count=$DISK_SIZE_MB status=progress

# Step 2: Create partition table with fdisk
echo "[2/5] Creating MBR partition table with one Minix partition..."
fdisk "$DISK_IMG" << EOF > /dev/null 2>&1
o
n
p
1
2048

t
81
w
EOF

# Step 3: Setup loop device
echo "[3/5] Setting up loop device..."
LOOP_DEV=$(losetup -f)
losetup -P "$LOOP_DEV" "$DISK_IMG"
echo "    Loop device: $LOOP_DEV"

# Wait for partition to appear
sleep 1

# Step 4: Format partition as Minix filesystem
echo "[4/5] Formatting partition 1 as Minix filesystem..."
mkfs.minix "${LOOP_DEV}p1" > /dev/null 2>&1

# Step 5: Verify and show partition info
echo "[5/5] Verifying partition table..."
fdisk -l "$DISK_IMG" | grep -E "(Disk|Device|${DISK_IMG})"

# Cleanup
losetup -d "$LOOP_DEV"
chmod 666 "$DISK_IMG"

echo ""
echo "=================================================="
echo "SUCCESS! Test disk created: $DISK_IMG"
echo "=================================================="
echo "Disk size: ${DISK_SIZE_MB}MB"
echo "Partition 1: Minix filesystem (type 0x81)"
echo "Partition size: ~${DISK_SIZE_MB}MB"
echo ""
echo "You can now attach this disk to your kernel for testing:"
echo "  - Add to bochsrc: ata0-master: type=disk, path=\"$DISK_IMG\""
echo "  - Or use as secondary disk in QEMU"
echo ""