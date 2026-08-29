#!/bin/bash
#
# Scepter — x86_64: clean everything, build, and run under QEMU (OVMF/EFI).
#
# Steps:
#   1. Remove all artifacts: build dirs, disk images, logs, symbols, mounts.
#   2. Build the x86_64 kernel (build-x86_64/kernel.elf) + userspace
#      (crt/build64).
#   3. Create root64.img (ext2 root fs) and efi.img (EFI GRUB ESP), then
#      install the 64-bit userspace into root64.img.
#   4. Boot with qemu-system-x86_64 + OVMF (the ESP on efi.img is auto-found;
#      EFI GRUB multiboot2 loads /boot/kernel64.elf; the kernel mounts
#      root64.img as hda1).
#
set -euo pipefail
cd "$(dirname "$0")"

ARCH=x86_64

echo "=============================================================="
echo " Scepter — $ARCH : clean, build, create disks, run"
echo "=============================================================="

echo "==> [1/5] Cleaning all artifacts (build dirs, disks, logs, symbols)..."
make clean
rm -f ./*.img ./*.log ./*.sym
rm -rf mnt

echo "==> [2/5] Building kernel ($ARCH)..."
make ARCH=$ARCH all

echo "==> [3/5] Building userspace (crt)..."
make -C crt ARCH=$ARCH all

echo "==> [4/5] Creating root64.img + efi.img and installing userspace..."
make ARCH=$ARCH root
make ARCH=$ARCH app

echo "==> [5/5] Booting $ARCH under QEMU (OVMF + EFI GRUB)..."
make ARCH=$ARCH run
