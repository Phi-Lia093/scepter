# ============================================================================
# Scepter Kernel - Top-Level Makefile
# ============================================================================

# Include common configuration
include common.mk

# Target
TARGET = $(BUILD_DIR)/kernel.elf

# Modules to build
MODULES = arch kernel mm lib driver fs net

# Automatically collect all .o files from build directory
# This is much cleaner than listing every single object file
KERNEL_OBJS = $(wildcard $(BUILD_DIR)/*.o)

.PHONY: all modules clean run debug root mount umount app

# Default target
all: $(BUILD_DIR) modules $(TARGET)
	@echo ""
	@echo "Build complete!"
	@ls -lh $(TARGET)

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Build all modules
modules:
	@echo "Building modules..."
	@for dir in $(MODULES); do \
		echo ""; \
		echo "==> Building $$dir"; \
		$(MAKE) -C $$dir || exit 1; \
	done
	@echo ""

# Link kernel
$(TARGET): modules
	@echo "Linking kernel..."
	@$(CC) $(LDFLAGS) -o $@ $(KERNEL_OBJS)
	nm --no-sort $@ > kernel.sym

# ============================================================================
# Architecture-specific disk/run parameters
# ============================================================================
#
# i386:   one disk (root.img) with GRUB (i386-pc) in the MBR + an ext2
#         partition that is both the boot partition and the root fs.
# x86_64: two disks — efi.img is a GPT ESP holding EFI GRUB (x86_64-efi,
#         multiboot2) + the kernel, root64.img is the ext2 root fs (hda1).
# ============================================================================
ifeq ($(ARCH),x86_64)
RUN_IMG     = efi.img          # disk GRUB + kernel live on (kernel copy target)
KERNEL_NAME = kernel64.elf
ROOT_FS_IMG = root64.img       # ext2 root filesystem (hda1)
CRT_ROOT    = crt/build64/root
QEMU_BIN    = qemu-system-x86_64
QEMU_BIOS   = -bios /usr/share/ovmf/OVMF.fd
QEMU_DRIVES = -drive file=root64.img,format=raw,if=ide,index=0 \
              -drive file=efi.img,format=raw,if=ide,index=1
else
RUN_IMG     = root.img         # GRUB MBR + ext2 root fs in one disk
KERNEL_NAME = kernel.elf
ROOT_FS_IMG = root.img
CRT_ROOT    = crt/build/root
QEMU_BIN    = qemu-system-i386
QEMU_BIOS   =
QEMU_DRIVES = -drive file=root.img,format=raw,if=ide,index=0
endif

# ============================================================================
# Root / Boot Disk Management
# ============================================================================
MOUNT_DIR = mnt
IMG ?= $(RUN_IMG)    # image to mount (override for app: IMG=root64.img)

root:
	@echo "Creating $(ARCH) boot/root disk image(s)..."
ifeq ($(ARCH),x86_64)
	@sudo ./script/make_grub_disk.sh root64.img nogrub
	@sudo ./script/make_efi_disk.sh
else
	@sudo ./script/make_grub_disk.sh root.img
endif
	@echo "Done! Use 'make app' to install the userspace, then 'make run'."

mount:
	@if [ ! -f $(IMG) ]; then \
		echo "ERROR: $(IMG) not found. Run 'make root' first."; \
		exit 1; \
	fi
	@echo "Mounting $(IMG) to ./$(MOUNT_DIR)..."
	@mkdir -p $(MOUNT_DIR)
	@sudo losetup -fP $(IMG)
	@LOOP=$$(losetup -j $(IMG) | cut -d: -f1); \
	sudo mount $${LOOP}p1 $(MOUNT_DIR); \
	sudo chown -R $(USER):$(USER) $(MOUNT_DIR); \
	echo "✓ Mounted at ./$(MOUNT_DIR) (owned by $(USER))"

umount:
	@echo "Unmounting ./$(MOUNT_DIR)..."
	@if mountpoint -q $(MOUNT_DIR); then \
		sudo umount $(MOUNT_DIR); \
	fi
	@LOOP=$$(losetup -j $(IMG) 2>/dev/null | cut -d: -f1); \
	if [ -n "$$LOOP" ]; then \
		sudo losetup -d $$LOOP; \
	fi
	@rmdir $(MOUNT_DIR) 2>/dev/null || true
	@echo "✓ Unmounted"

# ============================================================================
# Run and Debug (QEMU only)
#
# i386   boots from root.img via SeaBIOS + GRUB multiboot.
# x86_64 boots via OVMF (UEFI): the ESP on efi.img is found, EFI GRUB loads
# /boot/kernel64.elf with multiboot2, and the kernel mounts root64.img (hda1).
# 'make debug' pauses QEMU and exposes a GDB stub on tcp::1234.
# ============================================================================
run: $(TARGET)
	@$(MAKE) mount
	@echo "Copying kernel to disk..."
	@cp $(TARGET) $(MOUNT_DIR)/boot/$(KERNEL_NAME)
	@sync
	@$(MAKE) umount
	@echo "Starting QEMU ($(ARCH))..."
	@rm -f kernel.log
	@$(QEMU_BIN) -m 128 $(QEMU_BIOS) $(QEMU_DRIVES) \
		-serial file:kernel.log \
		-netdev user,id=net0 -device rtl8139,netdev=net0 \
		-no-reboot

debug: $(TARGET)
	@$(MAKE) mount
	@echo "Copying kernel to disk..."
	@cp $(TARGET) $(MOUNT_DIR)/boot/$(KERNEL_NAME)
	@sync
	@$(MAKE) umount
	@echo "Starting QEMU ($(ARCH)) with GDB stub on tcp::1234 (paused)..."
	@rm -f kernel.log
	@$(QEMU_BIN) -s -S -m 128 $(QEMU_BIOS) $(QEMU_DRIVES) \
		-serial file:kernel.log \
		-netdev user,id=net0 -device rtl8139,netdev=net0 \
		-no-reboot

app:
	@make -C crt all ARCH=$(ARCH)
	@echo "Mounting $(ROOT_FS_IMG) to install userspace..."
	@$(MAKE) mount IMG=$(ROOT_FS_IMG)
	@echo "Copying userspace programs to disk..."
	@rm -f mnt/bin/* mnt/init
	@cp $(CRT_ROOT)/init mnt/
	@mkdir -p mnt/bin
	@cp $(CRT_ROOT)/bin/* mnt/bin/
	@echo "✓ Userspace programs installed"
	@$(MAKE) umount IMG=$(ROOT_FS_IMG)

# ============================================================================
# Clean
# ============================================================================
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf build-i386 build-x86_64
	@rm -rf crt/build crt/build64
	@rm -f *.sym
	@echo "✓ Clean complete"

cleani:
	@echo "Cleaning build artifacts..."
	@rm -f *.img
	@echo "✓ Clean complete"
