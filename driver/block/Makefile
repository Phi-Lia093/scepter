# ============================================================================
# Block Driver Module Makefile
# ============================================================================

# Source files
SRCS = block.c ide.c cache.c part_mbr.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      driver/block/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all