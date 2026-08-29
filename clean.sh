#!/bin/bash
#
# Scepter — clean EVERYTHING: build dirs, crt build dirs, disk images,
# logs, symbol maps, and the mount point.
#
#   ./clean.sh
#
# After this the tree is pristine; the next ./run_*.sh (or ./build_*.sh +
# make root + make app + make run) rebuilds from scratch.
#
set -euo pipefail
cd "$(dirname "$0")"

echo "=============================================================="
echo " Scepter — clean all artifacts"
echo "=============================================================="

echo "==> Removing build dirs (build-i386, build-x86_64, crt/build, crt/build64)..."
# make clean handles the normal (user-owned) case; escalate to sudo only if
# a build dir is root-owned (e.g. left over from an interrupted build).
make clean >/dev/null 2>&1 || true
rm -rf build-i386 build-x86_64 crt/build crt/build64 2>/dev/null \
    || sudo rm -rf build-i386 build-x86_64 crt/build crt/build64

echo "==> Unmounting / detaching disk images (if any)..."
for img in root.img root64.img efi.img; do
    if [ -f "$img" ]; then
        loop=$(losetup -j "$img" 2>/dev/null | cut -d: -f1 | head -1)
        if [ -n "$loop" ]; then
            sudo umount "${loop}p1" >/dev/null 2>&1 || true
            sudo losetup -d "$loop" >/dev/null 2>&1 || true
        fi
    fi
done

echo "==> Removing disk images, logs, symbols, mount point..."
rm -f ./*.img ./*.log ./*.sym
rm -rf mnt 2>/dev/null || sudo rm -rf mnt

echo "✓ Clean complete"

