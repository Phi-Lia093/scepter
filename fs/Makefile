# ============================================================================
# Filesystem Module Makefile
# ============================================================================

# Source files
SRCS = vfs.c devfs.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      fs/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all