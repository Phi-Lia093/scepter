# ============================================================================
# Library Module Makefile
# ============================================================================

# Source files
SRCS = printk.c string.c list.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      lib/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all