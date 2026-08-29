#!/bin/bash
#
# Scepter — run x86_64 under QEMU (OVMF + EFI GRUB).
#
# Does NOT clean: it builds incrementally (make decides what to remake),
# uses the current root64.img + efi.img if present (creating them only when
# missing), refreshes the userspace + kernel, then boots qemu-system-x86_64
# with OVMF (the ESP on efi.img is auto-found; EFI GRUB multiboot2 loads
# /boot/kernel64.elf; the kernel mounts root64.img as hda1).
#
#   ./clean.sh            # remove everything first (optional)
#   ./run_x86_64.sh       # build -> ensure disks -> install -> boot
#
set -euo pipefail
cd "$(dirname "$0")"

echo "=============================================================="
echo " Scepter — x86_64 : build, prepare current disks, run"
echo "=============================================================="

echo "==> [1/3] Building (kernel + userspace)..."
./build_x86_64.sh

echo "==> [2/3] Preparing root64.img + efi.img..."
if [ ! -f root64.img ] || [ ! -f efi.img ]; then
    echo "    root64.img / efi.img not found — creating them..."
    make ARCH=x86_64 root
fi
make ARCH=x86_64 app

echo "==> [3/3] Booting x86_64 under QEMU (OVMF + EFI GRUB)..."
make ARCH=x86_64 run
