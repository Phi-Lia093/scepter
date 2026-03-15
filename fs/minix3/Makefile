# ============================================================================
# Minix v3 Filesystem Module Makefile
# ============================================================================

# Source files
SRCS = minix3.c super.c inode.c dir.c file.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      fs/minix3/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all