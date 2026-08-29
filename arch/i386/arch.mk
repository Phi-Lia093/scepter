# ============================================================================
# i386 architecture configuration
# Included by common.mk when ARCH=i386.  Sets the toolchain flags that are
# specific to this architecture.
# ============================================================================

# i386: 32-bit code, no red zone (kernel ABI), no PIC/PIE
ARCH_CFLAGS   = -m32 -mno-red-zone -fno-pic -fno-pie
ARCH_LDFLAGS  = -m32

export ARCH_CFLAGS ARCH_LDFLAGS
