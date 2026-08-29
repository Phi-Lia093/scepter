# ============================================================================
# Networking Module Makefile
# ============================================================================

# Source files
SRCS = net.c loopback.c proto.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      net/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all
