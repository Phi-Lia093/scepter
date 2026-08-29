# ============================================================================
# Makefile for C Runtime Library and Core Utilities
# ============================================================================

CC = gcc
AS = gcc
AR = ar
LD = gcc
OBJCOPY = objcopy

# Architecture: i386 (default) or x86_64
ARCH ?= i386

ifeq ($(ARCH),x86_64)
ABI_FLAGS      = -m64 -mno-red-zone -fno-pic -fno-pie
CRT0_SOURCE    = libc/crt0_64.s
LINKER_SCRIPT  = linker64.ld
BUILD_DIR      = build64
else
ABI_FLAGS      = -m32 -mno-red-zone -fno-pic -fno-pie
CRT0_SOURCE    = libc/crt0.s
LINKER_SCRIPT  = linker.ld
BUILD_DIR      = build
endif

CFLAGS = -c -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
         -O2 -Wall -Wextra -fomit-frame-pointer \
         $(ABI_FLAGS) -I ./include
ASFLAGS = -c -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
          -O2 -Wall -Wextra \
          $(ABI_FLAGS) -I ./include
ARFLAGS = rcs
LDFLAGS = -T $(LINKER_SCRIPT) -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
          -O2 -Wall -Wextra $(ABI_FLAGS) -no-pie

ROOT_DIR  = $(BUILD_DIR)/root
BIN_DIR   = $(ROOT_DIR)/bin

LIBC = $(BUILD_DIR)/libc.a
CRT0 = $(BUILD_DIR)/crt0.o

# ============================================================================
# libc sources
# ============================================================================

LIBC_C_SOURCES = \
    libc/stdio/putchar.c \
    libc/stdio/puts.c \
    libc/stdio/vsnprintf.c \
    libc/stdio/stdio_extra.c \
    libc/string/strlen.c \
    libc/string/strcmp.c \
    libc/string/strcpy.c \
    libc/string/memset.c \
    libc/string/memcpy.c \
    libc/string/string_extra.c \
    libc/string/string_more.c \
    libc/stdlib/atoi.c \
    libc/stdlib/malloc.c \
    libc/stdlib/stdlib_extra.c \
    libc/stdlib/environ.c \
    libc/ctype/isdigit.c \
    libc/ctype/isalpha.c \
    libc/ctype/isspace.c \
    libc/errno.c \
    libc/dirent.c \
    libc/time/time.c \
    libc/process/exec.c \
    libc/process/getopt.c \
    libc/misc.c

SYSCALL_SOURCES = syscall.c

LIBC_OBJECTS = $(addprefix $(BUILD_DIR)/, $(LIBC_C_SOURCES:.c=.o))
SYSCALL_OBJECTS = $(addprefix $(BUILD_DIR)/, $(SYSCALL_SOURCES:.c=.o))
ALL_LIB_OBJECTS = $(LIBC_OBJECTS) $(SYSCALL_OBJECTS)

# ============================================================================
# Core utilities (init goes to /, the rest to /bin)
# ============================================================================

COREUTILS = sh ls cat echo mkdir rm rmdir mv cp pwd clear uname \
            sleep env true false seq which head tail wc tee date \
            uptime free ps stat cmp beep \
            touch ln chmod chown readlink basename dirname test [ printf \
            yes sort uniq grep find cut tr dd df du od strings cksum xargs file \
            kill killall time hostname whoami id reboot poweroff halt \
            mount umount top watch stty dmesg ifconfig ping
INIT_BIN  = $(ROOT_DIR)/init
CORE_BINS = $(addprefix $(BIN_DIR)/, $(COREUTILS))

.PHONY: all clean install

all: $(LIBC) $(CRT0) $(INIT_BIN) $(CORE_BINS)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/libc/stdio
	@mkdir -p $(BUILD_DIR)/libc/string
	@mkdir -p $(BUILD_DIR)/libc/stdlib
	@mkdir -p $(BUILD_DIR)/libc/ctype
	@mkdir -p $(BUILD_DIR)/coreutil
	@mkdir -p $(ROOT_DIR)
	@mkdir -p $(BIN_DIR)

$(LIBC): $(BUILD_DIR) $(ALL_LIB_OBJECTS)
	@echo "  AR      $@"
	@$(AR) $(ARFLAGS) $@ $(ALL_LIB_OBJECTS)

$(CRT0): $(CRT0_SOURCE) | $(BUILD_DIR)
	@echo "  AS      $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "  CC      $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

# ============================================================================
# Programs: generic link rule for /bin/<name>, special rule for /init
# ============================================================================

$(BIN_DIR)/%: $(CRT0) $(LIBC) coreutil/%.c | $(BUILD_DIR)
	@echo "  CC      coreutil/$*.c"
	@$(CC) $(CFLAGS) -c coreutil/$*.c -o $(BUILD_DIR)/coreutil/$*.o
	@echo "  LD      $(BUILD_DIR)/coreutil/$*.elf"
	@$(LD) $(LDFLAGS) -o $(BUILD_DIR)/coreutil/$*.elf $(CRT0) $(BUILD_DIR)/coreutil/$*.o $(LIBC)
	@$(OBJCOPY) -O binary $(BUILD_DIR)/coreutil/$*.elf $@
	@echo "built: $@"

$(INIT_BIN): $(CRT0) $(LIBC) coreutil/init.c | $(BUILD_DIR)
	@echo "  CC      coreutil/init.c"
	@$(CC) $(CFLAGS) -c coreutil/init.c -o $(BUILD_DIR)/coreutil/init.o
	@echo "  LD      $(BUILD_DIR)/coreutil/init.elf"
	@$(LD) $(LDFLAGS) -o $(BUILD_DIR)/coreutil/init.elf $(CRT0) $(BUILD_DIR)/coreutil/init.o $(LIBC)
	@$(OBJCOPY) -O binary $(BUILD_DIR)/coreutil/init.elf $@
	@echo "built: $@"

# ============================================================================
# Install / clean
# ============================================================================

install:
	@echo "Root filesystem ready at: $(ROOT_DIR)"
	@find $(ROOT_DIR) -type f | sort

clean:
	@rm -rf $(BUILD_DIR)
	@echo "cleaned crt build artifacts"
