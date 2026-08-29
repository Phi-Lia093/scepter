# ============================================================================
# Network Driver Module Makefile
# ============================================================================

# Source files
SRCS = rtl8139.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      driver/net/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all
