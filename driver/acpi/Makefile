# ============================================================================
# ACPI Driver Makefile
# ============================================================================

# Source files
SRCS = acpi.c rsdp.c tables.c shutdown.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      acpi/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all