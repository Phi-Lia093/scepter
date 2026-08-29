#!/bin/bash
#
# Create efi.img: a GPT disk with an EFI System Partition that holds the
# x86_64 EFI GRUB (with the multiboot2 module) and the kernel.  OVMF finds
# the ESP automatically; GRUB then loads /boot/kernel64.elf with multiboot2,
# and the kernel mounts root64.img (the ext2 root fs) as hda1.
#
# Usage: sudo ./script/make_efi_disk.sh
#
# Outputs:
#   efi.img           64 MB GPT disk, one FAT32 EFI System Partition
#   EFI/BOOT/BOOTX64.EFI   GRUB (removable-media path, auto-found by OVMF)
#   boot/grub/grub.cfg     multiboot2 menuentry
#   boot/kernel64.elf      kernel (copied if build-x86_64/kernel.elf exists;
#                          'make run' refreshes it before booting)
#
# Requires: grub-install (x86_64-efi modules), sgdisk, mkfs.fat, dosfstools
#

set -e  # Exit on error

ESP_IMG="efi.img"
ESP_SIZE_MB=64

echo "=============================================="
echo "Creating EFI GRUB Disk Image ($ESP_IMG)"
echo "=============================================="

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "ERROR: This script must be run as root (use sudo)"
    exit 1
fi

# Step 1: Create blank disk image + GPT partition table with one EFI System partition
echo "[1/6] Creating ${ESP_SIZE_MB}MB disk image with GPT partition table..."
dd if=/dev/zero of="$ESP_IMG" bs=1M count=$ESP_SIZE_MB status=progress
sgdisk -o -n 1:2048:0 -t 1:ef00 "$ESP_IMG" > /dev/null

# Step 2: Setup loop device
echo "[2/6] Setting up loop device..."
LOOP_DEV=$(losetup -f)
losetup -P "$LOOP_DEV" "$ESP_IMG"
echo "    Loop device: $LOOP_DEV"
sleep 1

# Step 3: Format partition as FAT32 (EFI System Partition)
echo "[3/6] Formatting ESP as FAT32..."
mkfs.fat -F32 "${LOOP_DEV}p1" > /dev/null

# Step 4: Mount partition temporarily
echo "[4/6] Mounting ESP..."
TEMP_MOUNT=$(mktemp -d)
mount "${LOOP_DEV}p1" "$TEMP_MOUNT"

# Step 5: Install EFI GRUB with the multiboot2 module (removable-media path:
# EFI/BOOT/BOOTX64.EFI is what OVMF looks for on the ESP).
echo "[5/6] Installing EFI GRUB (x86_64-efi)..."
grub-install --target=x86_64-efi \
    --efi-directory="$TEMP_MOUNT" \
    --boot-directory="$TEMP_MOUNT/boot" \
    --removable \
    --modules='multiboot2 normal ext2 part_gpt part_msdos fat' 2>&1 | grep -v Installing

# Step 6: grub.cfg + kernel
echo "[6/6] Writing grub.cfg and staging the kernel..."
cat > "$TEMP_MOUNT/boot/grub/grub.cfg" << 'EOF'
set timeout=2
set default=0

menuentry "scepter x86_64" {
    insmod multiboot2
    multiboot2 /boot/kernel64.elf
    boot
}
EOF
mkdir -p "$TEMP_MOUNT/boot"
if [ -f build-x86_64/kernel.elf ]; then
    cp build-x86_64/kernel.elf "$TEMP_MOUNT/boot/kernel64.elf"
    echo "    staged build-x86_64/kernel.elf -> /boot/kernel64.elf"
fi

# Cleanup
sync
umount "$TEMP_MOUNT"
rmdir "$TEMP_MOUNT"
losetup -d "$LOOP_DEV"
chmod 666 "$ESP_IMG"

echo ""
echo "=============================================="
echo "SUCCESS! EFI GRUB disk created: $ESP_IMG"
echo "=============================================="
echo "  Partition 1: FAT32 EFI System Partition"
echo "  EFI/BOOT/BOOTX64.EFI  -> EFI GRUB (multiboot2)"
echo "  /boot/kernel64.elf    -> x86_64 kernel"
echo "  /boot/grub/grub.cfg   -> multiboot2 menuentry"
echo ""
echo "Boot with: qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \\"
echo "    -drive file=root64.img,format=raw,if=ide,index=0 \\"
echo "    -drive file=efi.img,format=raw,if=ide,index=1"
echo ""