# ============================================================================
# x86_64 Architecture Module Makefile
# Builds all arch-specific objects into the top-level build directory.
# ============================================================================

# Source files (populated as the port progresses)
SRCS_C = cpu.c irq.c pic.c lapic.c ioapic.c paging.c mem.c timer.c trap.c uaccess.c
SRCS_S = boot.s context.s isr.s

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS_C:.c=.o) $(SRCS_S:.s=.o))

# Build all objects
all: $(OBJS)

# C source compilation
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      arch/x86_64/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

# Assembly source compilation
$(BUILD_DIR)/%.o: %.s
	@echo "  AS      arch/x86_64/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all
