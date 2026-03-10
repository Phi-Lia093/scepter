AS      = i686-linux-gnu-as
CC      = i686-linux-gnu-gcc
LD      = i686-linux-gnu-ld
OBJCOPY = i686-linux-gnu-objcopy

CFLAGS  = -c -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
          -fno-pie -mno-red-zone -O100 -Wall -Wextra -I include -I kernel \
          -fno-pic -m32
LIBGCC  = $(shell i686-linux-gnu-gcc -print-libgcc-file-name)
LDFLAGS = -T linker.ld -nostdlib -Map=kernel.sym

BUILD   = build
TARGET  = $(BUILD)/kernel.elf

KERNEL_OBJS = $(BUILD)/boot.o \
              $(BUILD)/kernel.o \
              $(BUILD)/cpu.o \
              $(BUILD)/printk.o \
              $(BUILD)/string.o \
              $(BUILD)/vga.o \
              $(BUILD)/pic.o \
              $(BUILD)/isr.o \
              $(BUILD)/panic.o \
              $(BUILD)/pit.o \
              $(BUILD)/buddy.o \
              $(BUILD)/slab.o \
              $(BUILD)/driver.o \
              $(BUILD)/tty.o \
              $(BUILD)/kbd.o \
              $(BUILD)/ide.o \
              $(BUILD)/cache.o \
              $(BUILD)/part_mbr.o \
              $(BUILD)/vfs.o \

.PHONY: all clean run debug

all: $(BUILD) $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

# ===========================================================================
# Kernel Build
# ===========================================================================
$(BUILD)/boot.o: kernel/boot.s
	$(AS) kernel/boot.s -o $@

$(BUILD)/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) kernel/kernel.c -o $@

$(BUILD)/cpu.o: kernel/cpu.c include/kernel/cpu.h kernel/asm.h
	$(CC) $(CFLAGS) kernel/cpu.c -o $@

$(BUILD)/printk.o: lib/printk.c include/lib/printk.h include/driver/char/vga.h
	$(CC) $(CFLAGS) lib/printk.c -o $@

$(BUILD)/string.o: lib/string.c include/lib/string.h
	$(CC) $(CFLAGS) lib/string.c -o $@

$(BUILD)/vga.o: driver/char/vga.c include/driver/char/vga.h kernel/asm.h
	$(CC) $(CFLAGS) driver/char/vga.c -o $@

$(BUILD)/driver.o: driver/driver.c include/driver/driver.h include/mm/slab.h
	$(CC) $(CFLAGS) driver/driver.c -o $@

$(BUILD)/tty.o: driver/char/tty.c include/driver/char/tty.h include/driver/char/vga.h include/driver/driver.h
	$(CC) $(CFLAGS) driver/char/tty.c -o $@

$(BUILD)/kbd.o: driver/char/kbd.c include/driver/char/kbd.h include/driver/driver.h include/driver/pic.h include/kernel/cpu.h kernel/asm.h
	$(CC) $(CFLAGS) driver/char/kbd.c -o $@

$(BUILD)/pic.o: driver/pic.c include/driver/pic.h kernel/asm.h
	$(CC) $(CFLAGS) driver/pic.c -o $@

$(BUILD)/isr.o: kernel/isr.s
	$(AS) kernel/isr.s -o $@

$(BUILD)/panic.o: kernel/panic.c include/kernel/panic.h include/lib/printk.h kernel/asm.h
	$(CC) $(CFLAGS) kernel/panic.c -o $@

$(BUILD)/pit.o: driver/char/pit.c include/driver/char/pit.h include/driver/pic.h include/kernel/cpu.h include/driver/driver.h kernel/asm.h
	$(CC) $(CFLAGS) driver/char/pit.c -o $@

$(BUILD)/buddy.o: mm/buddy.c include/mm/buddy.h include/lib/printk.h
	$(CC) $(CFLAGS) mm/buddy.c -o $@

$(BUILD)/slab.o: mm/slab.c include/mm/slab.h include/mm/buddy.h include/lib/printk.h
	$(CC) $(CFLAGS) mm/slab.c -o $@

$(BUILD)/ide.o: driver/block/ide.c include/driver/block/ide.h kernel/asm.h include/lib/printk.h
	$(CC) $(CFLAGS) driver/block/ide.c -o $@

$(BUILD)/cache.o: driver/block/cache.c include/driver/block/cache.h include/driver/driver.h include/mm/slab.h include/lib/printk.h
	$(CC) $(CFLAGS) driver/block/cache.c -o $@

$(BUILD)/part_mbr.o: driver/block/part_mbr.c include/driver/block/part_mbr.h include/driver/driver.h include/driver/block/ide.h include/lib/printk.h
	$(CC) $(CFLAGS) driver/block/part_mbr.c -o $@

$(BUILD)/vfs.o: fs/vfs.c include/fs/fs.h include/mm/slab.h include/lib/printk.h
	$(CC) $(CFLAGS) fs/vfs.c -o $@

# Link kernel as ELF (Multiboot compatible)
$(TARGET): $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS) $(LIBGCC)
	@echo "Kernel ELF created: $@"
	@ls -lh $@

# ===========================================================================
# GRUB Disk Management
# ===========================================================================
MOUNT_DIR = mnt

grub:
	@echo "Creating clean GRUB disk image..."
	sudo ./script/make_grub_disk.sh
	@echo "Done! Use 'make mount' to mount the disk."

data:
	sudo ./script/make_test_disk.sh

mount:
	@if [ ! -f disk.img ]; then \
		echo "ERROR: disk.img not found. Run 'make init' first."; \
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

# ===========================================================================
# Run and Debug
# ===========================================================================
run: $(TARGET)
	@if [ ! -f disk.img ]; then \
		echo "ERROR: disk.img not found. Run 'make init' first."; \
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
	qemu-system-i386 -m 128 -drive file=disk.img,format=raw,if=ide,index=0,media=disk -drive file=data.img,format=raw,if=ide,index=1,media=disk

debug: $(TARGET)
	@if [ ! -f disk.img ]; then \
		echo "ERROR: disk.img not found. Run 'make init' first."; \
		exit 1; \
	fi
	@if ! mountpoint -q $(MOUNT_DIR); then \
		$(MAKE) mount; \
	fi
	@cp $(TARGET) $(MOUNT_DIR)/boot/kernel.elf
	@sync
	@$(MAKE) umount
	bochs

clean:
	rm -rf $(BUILD)
