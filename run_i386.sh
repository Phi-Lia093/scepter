#!/bin/bash
#
# Scepter — i386: clean everything, build, and run under QEMU.
#
# Steps:
#   1. Remove all artifacts: build dirs, disk images, logs, symbols, mounts.
#   2. Build the i386 kernel (build-i386/kernel.elf) + userspace (crt/build).
#   3. Create root.img (GRUB MBR + ext2 root fs) and install the userspace.
#   4. Boot with qemu-system-i386.
#
set -euo pipefail
cd "$(dirname "$0")"

ARCH=i386

echo "=============================================================="
echo " Scepter — $ARCH : clean, build, create disk, run"
echo "=============================================================="

echo "==> [1/5] Cleaning all artifacts (build dirs, disks, logs, symbols)..."
make clean
rm -f ./*.img ./*.log ./*.sym
rm -rf mnt

echo "==> [2/5] Building kernel ($ARCH)..."
make ARCH=$ARCH all

echo "==> [3/5] Building userspace (crt)..."
make -C crt ARCH=$ARCH all

echo "==> [4/5] Creating root.img and installing userspace..."
make ARCH=$ARCH root
make ARCH=$ARCH app

echo "==> [5/5] Booting $ARCH under QEMU..."
make ARCH=$ARCH run
