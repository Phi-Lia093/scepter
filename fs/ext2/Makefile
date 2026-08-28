# ============================================================================
# ext2 Filesystem Module Makefile
# ============================================================================

# Source files
SRCS = ext2.c ext2_super.c ext2_inode.c ext2_dir.c ext2_file.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      fs/ext2/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all
