# ============================================================================
# Character Driver Module Makefile
# ============================================================================

# Source files
SRCS = char.c vga.c tty.c kbd.c pit.c serial.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      driver/char/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all