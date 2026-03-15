# ============================================================================
# Scepter Kernel - Top-Level Makefile
# ============================================================================

# Include common configuration
include common.mk

# Target
TARGET = $(BUILD_DIR)/kernel.elf

# Modules to build
MODULES = kernel mm lib driver fs

# All object files will be in build/ directory
KERNEL_OBJS = $(BUILD_DIR)/boot.o \
              $(BUILD_DIR)/kernel.o \
              $(BUILD_DIR)/cpu.o \
              $(BUILD_DIR)/panic.o \
              $(BUILD_DIR)/sched.o \
              $(BUILD_DIR)/isr.o \
              $(BUILD_DIR)/mminit.o \
              $(BUILD_DIR)/buddy.o \
              $(BUILD_DIR)/slab.o \
              $(BUILD_DIR)/printk.o \
              $(BUILD_DIR)/string.o \
              $(BUILD_DIR)/list.o \
              $(BUILD_DIR)/pic.o \
              $(BUILD_DIR)/char.o \
              $(BUILD_DIR)/vga.o \
              $(BUILD_DIR)/tty.o \
              $(BUILD_DIR)/kbd.o \
              $(BUILD_DIR)/pit.o \
              $(BUILD_DIR)/serial.o \
              $(BUILD_DIR)/block.o \
              $(BUILD_DIR)/ide.o \
              $(BUILD_DIR)/cache.o \
              $(BUILD_DIR)/part_mbr.o \
              $(BUILD_DIR)/vfs.o \
              $(BUILD_DIR)/devfs.o \
              $(BUILD_DIR)/minix3.o \
              $(BUILD_DIR)/super.o \
              $(BUILD_DIR)/inode.o \
              $(BUILD_DIR)/dir.o \
              $(BUILD_DIR)/file.o

.PHONY: all modules clean run debug grub data mount umount mountd umountd

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

# ===========================================================================
# GRUB Disk Management
# ===========================================================================
MOUNT_DIR = mnt

grub:
	@echo "Creating clean GRUB disk image..."
	@sudo ./script/make_grub_disk.sh
	@echo "Done! Use 'make mount' to mount the disk."

data:
	@sudo ./script/make_test_disk.sh

mount:
	@if [ ! -f disk.img ]; then \
		echo "ERROR: disk.img not found. Run 'make grub' first."; \
		exit 1; \
	fi
	@echo "Mounting disk.img to ./$(MOUNT_DIR)..."
	@mkdir -p $(MOUNT_DIR)
	@sudo losetup -fP disk.img
	@LOOP=$$(losetup -j disk.img | cut -d: -f1); \
	sudo mount $${LOOP}p1 $(MOUNT_DIR); \
	sudo chown -R $(USER):$(USER) $(MOUNT_DIR); \
	echo "✓ Mounted at ./$(MOUNT_DIR) (owned by $(USER))"

umount:
	@echo "Unmounting ./$(MOUNT_DIR)..."
	@if mountpoint -q $(MOUNT_DIR); then \
		sudo umount $(MOUNT_DIR); \
	fi
	@LOOP=$$(losetup -j disk.img 2>/dev/null | cut -d: -f1); \
	if [ -n "$$LOOP" ]; then \
		sudo losetup -d $$LOOP; \
	fi
	@rmdir $(MOUNT_DIR) 2>/dev/null || true
	@echo "✓ Unmounted"

mountd:
	@if [ ! -f data.img ]; then \
		echo "ERROR: data.img not found. Run 'make data' first."; \
		exit 1; \
	fi
	@echo "Mounting data.img to ./$(MOUNT_DIR)..."
	@mkdir -p $(MOUNT_DIR)
	@sudo losetup -fP data.img
	@LOOP=$$(losetup -j data.img | cut -d: -f1); \
	sudo mount $${LOOP}p1 $(MOUNT_DIR); \
	sudo chown -R $(USER):$(USER) $(MOUNT_DIR); \
	echo "✓ Mounted at ./$(MOUNT_DIR) (owned by $(USER))"

umountd:
	@echo "Unmounting ./$(MOUNT_DIR)..."
	@if mountpoint -q $(MOUNT_DIR); then \
		sudo umount $(MOUNT_DIR); \
	fi
	@LOOP=$$(losetup -j data.img 2>/dev/null | cut -d: -f1); \
	if [ -n "$$LOOP" ]; then \
		sudo losetup -d $$LOOP; \
	fi
	@rmdir $(MOUNT_DIR) 2>/dev/null || true
	@echo "✓ Unmounted"

# ===========================================================================
# Run and Debug
# ===========================================================================
run: $(TARGET)
	@if [ ! -f disk.img ]; then \
		echo "ERROR: disk.img not found. Run 'make grub' first."; \
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
	@echo "Starting QEMU..."
	@rm -f kernel.log
	@qemu-system-i386 -m 128 \
		-drive file=disk.img,format=raw,if=ide,index=0,media=disk \
		-drive file=data.img,format=raw,if=ide,index=1,media=disk \
		-serial file:kernel.log

debug: $(TARGET)
	@if [ ! -f disk.img ]; then \
		echo "ERROR: disk.img not found. Run 'make grub' first."; \
		exit 1; \
	fi
	@if ! mountpoint -q $(MOUNT_DIR); then \
		$(MAKE) mount; \
	fi
	@cp $(TARGET) $(MOUNT_DIR)/boot/kernel.elf
	@sync
	@$(MAKE) umount
	@rm -f kernel.log
	@bochs

# ===========================================================================
# Clean
# ===========================================================================
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -f *.sym
	@echo "✓ Clean complete"

cleani:
	@echo "Cleaning build artifacts..."
	@rm -f *.img
	@echo "✓ Clean complete"