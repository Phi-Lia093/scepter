#!/bin/bash
#
# Scepter — run i386 under QEMU.
#
# Does NOT clean: it builds incrementally (make decides what to remake),
# uses the current root.img if present (creating it only when missing),
# refreshes the userspace + kernel on it, then boots qemu-system-i386.
#
#   ./clean.sh            # remove everything first (optional)
#   ./run_i386.sh         # build -> ensure disk -> install -> boot
#
set -euo pipefail
cd "$(dirname "$0")"

echo "=============================================================="
echo " Scepter — i386 : build, prepare current disk, run"
echo "=============================================================="

echo "==> [1/3] Building (kernel + userspace)..."
./build_i386.sh

echo "==> [2/3] Preparing root.img..."
if [ ! -f root.img ]; then
    echo "    root.img not found — creating it..."
    make ARCH=i386 root
fi
make ARCH=i386 app

echo "==> [3/3] Booting i386 under QEMU..."
make ARCH=i386 run
