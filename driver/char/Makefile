# ============================================================================
# Character Driver Module Makefile
# ============================================================================

# Source files (pit.c = system timer moved to arch/i386/timer.c)
SRCS = char.c vga.c tty.c kbd.c serial.c rtc.c miscdev.c random.c pcspk.c mouse.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      driver/char/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all