#!/bin/bash
#
# Scepter — build the i386 kernel + userspace.
#
# Does NOT clean: make's dependency tracking decides what to rebuild, so
# unchanged sources are left untouched.  Run ./clean.sh first for a
# from-scratch build.
#
set -euo pipefail
cd "$(dirname "$0")"

echo "==> Building kernel (i386)..."
make ARCH=i386 all

echo "==> Building userspace (crt)..."
make -C crt ARCH=i386 all

echo "✓ i386 build ready"
