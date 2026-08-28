# ============================================================================
# Scepter Kernel - Top-Level Makefile
# ============================================================================

# Include common configuration
include common.mk

# Target
TARGET = $(BUILD_DIR)/kernel.elf

# Modules to build
MODULES = kernel mm lib driver fs

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

# ===========================================================================
# Root Disk Management
#
# One AHCI disk image (root.img) holds everything: GRUB in the MBR and a
# single minix v3 partition (sda1) that is both the boot partition
# (/boot/kernel.elf) and the root filesystem (/init, /bin; /dev is devfs
# automounted by init).  'make run' attaches it to an ICH9 AHCI controller.
# ===========================================================================
MOUNT_DIR = mnt

root:
	@echo "Creating root disk image (GRUB + root filesystem)..."
	@sudo ./script/make_grub_disk.sh
	@echo "Done! Use 'make mount' to mount it, then 'make app'."

mount:
	@if [ ! -f root.img ]; then \
		echo "ERROR: root.img not found. Run 'make root' first."; \
		exit 1; \
	fi
	@echo "Mounting root.img to ./$(MOUNT_DIR)..."
	@mkdir -p $(MOUNT_DIR)
	@sudo losetup -fP root.img
	@LOOP=$$(losetup -j root.img | cut -d: -f1); \
	sudo mount $${LOOP}p1 $(MOUNT_DIR); \
	sudo chown -R $(USER):$(USER) $(MOUNT_DIR); \
	echo "✓ Mounted at ./$(MOUNT_DIR) (owned by $(USER))"

umount:
	@echo "Unmounting ./$(MOUNT_DIR)..."
	@if mountpoint -q $(MOUNT_DIR); then \
		sudo umount $(MOUNT_DIR); \
	fi
	@LOOP=$$(losetup -j root.img 2>/dev/null | cut -d: -f1); \
	if [ -n "$$LOOP" ]; then \
		sudo losetup -d $$LOOP; \
	fi
	@rmdir $(MOUNT_DIR) 2>/dev/null || true
	@echo "✓ Unmounted"

# ===========================================================================
# Run and Debug
#
# The whole system now boots and runs in AHCI mode: root.img is attached to
# an emulated ICH9 AHCI controller (SeaBIOS + GRUB boot from SATA; the
# kernel mounts the root filesystem from sda1).  No IDE drives are used.
# ===========================================================================
run: $(TARGET)
	@if [ ! -f root.img ]; then \
		echo "ERROR: root.img not found. Run 'make root' first."; \
		exit 1; \
	fi
	@if ! mountpoint -q $(MOUNT_DIR); then \
		echo "Mounting disk..."; \
		$(MAKE) mount; \
	fi
	@echo "Copying kernel to disk..."
	@cp $(TARGET) $(MOUNT_DIR)/boot/kernel.elf
	@sync
	@echo "Unmounting disk..."
	@$(MAKE) umount
	@echo "Starting QEMU (AHCI root disk)..."
	@rm -f kernel.log
	@qemu-system-i386 -m 128 \
		-drive file=root.img,format=raw,if=none,id=sata0 \
		-device ich9-ahci,id=ahci \
		-device ide-hd,drive=sata0,bus=ahci.0 \
		-serial file:kernel.log
		-accel kvm



debug: $(TARGET)
	@if [ ! -f root.img ]; then \
		echo "ERROR: root.img not found. Run 'make root' first."; \
		exit 1; \
	fi
	@if ! mountpoint -q $(MOUNT_DIR); then \
		echo "Mounting disk..."; \
		$(MAKE) mount; \
	fi
	@echo "Copying kernel to disk..."
	@cp $(TARGET) $(MOUNT_DIR)/boot/kernel.elf
	@sync
	@echo "Unmounting disk..."
	@$(MAKE) umount
	@echo "Starting bochs..."
	@rm -f kernel.log
	@bochs

app:
	@make mount
	@make -C crt all
	@echo "Copying userspace programs to disk..."
	@rm -f mnt/bin/* mnt/init
	@cp crt/build/root/init mnt/
	@mkdir -p mnt/bin
	@cp crt/build/root/bin/* mnt/bin/
	@echo "✓ Userspace programs installed"
	@make umount

# ===========================================================================
# Clean
# ===========================================================================
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@make -C crt clean
	@rm -f *.sym
	@echo "✓ Clean complete"

cleani:
	@echo "Cleaning build artifacts..."
	@rm -f *.img
	@echo "✓ Clean complete"
