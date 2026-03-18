# ============================================================================
# APIC Driver Makefile
# ============================================================================

# Source files
SRCS = lapic.c ioapic.c interrupt.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      apic/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all