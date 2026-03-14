# ============================================================================
# Common Build Configuration
# Shared by all module Makefiles
# ============================================================================

# Compiler and flags
CC      = gcc
CFLAGS  = -c -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
          -fno-pie -mno-red-zone -O2 -Wall -Wextra -fno-pic -m32

LDFLAGS = -T linker.ld -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
          -fno-pie -mno-red-zone -O2 -Wall -Wextra -fno-pic -m32

# Directories
TOP_DIR    := $(shell pwd)
BUILD_DIR  := $(TOP_DIR)/build
INCLUDE    := -I $(TOP_DIR)/include -I $(TOP_DIR)/kernel

# Export for sub-makes
export CC CFLAGS LDFLAGS TOP_DIR BUILD_DIR INCLUDE