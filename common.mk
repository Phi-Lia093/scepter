# ============================================================================
# Common Build Configuration
# Shared by all module Makefiles
# ============================================================================

# Directories
TOP_DIR    := $(shell pwd)

# Architecture selection (i386 default; x86_64 in progress)
ARCH      ?= i386
ARCH_DIR  := $(TOP_DIR)/arch/$(ARCH)

BUILD_DIR  := $(TOP_DIR)/build-$(ARCH)

# Include architecture-specific toolchain flags
include $(ARCH_DIR)/arch.mk

# Compiler and flags
CC      = gcc
CFLAGS  = -c -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
          -O100 -Wall -Wextra $(ARCH_CFLAGS)

LDFLAGS = -T $(ARCH_DIR)/linker.ld -ffreestanding -nostdlib -fno-builtin \
          -fno-stack-protector -O100 -Wall -Wextra $(ARCH_LDFLAGS)

INCLUDE    := -I $(TOP_DIR)/include -I $(TOP_DIR)/kernel -I $(ARCH_DIR)/include

# Export for sub-makes
export ARCH ARCH_DIR CC CFLAGS LDFLAGS TOP_DIR BUILD_DIR INCLUDE