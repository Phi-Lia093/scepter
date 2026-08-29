#!/bin/bash
#
# Scepter — build the x86_64 kernel + userspace.
#
# Does NOT clean: make's dependency tracking decides what to rebuild, so
# unchanged sources are left untouched.  Run ./clean.sh first for a
# from-scratch build.
#
set -euo pipefail
cd "$(dirname "$0")"

echo "==> Building kernel (x86_64)..."
make ARCH=x86_64 all

echo "==> Building userspace (crt)..."
make -C crt ARCH=x86_64 all

echo "✓ x86_64 build ready"
