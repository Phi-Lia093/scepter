# ============================================================================
# x86_64 architecture configuration
# Included by common.mk when ARCH=x86_64.  Sets the toolchain flags that are
# specific to this architecture.
# ============================================================================

# x86_64: 64-bit kernel code model, no red zone (kernel ABI), no PIC/PIE
ARCH_CFLAGS   = -m64 -mcmodel=kernel -mno-red-zone -fno-pic -fno-pie
ARCH_LDFLAGS  = -m64 -no-pie

export ARCH_CFLAGS ARCH_LDFLAGS
