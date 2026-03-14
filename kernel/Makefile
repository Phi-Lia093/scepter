# ============================================================================
# Kernel Module Makefile
# ============================================================================

# Source files
SRCS_C = kernel.c cpu.c panic.c sched.c
SRCS_S = boot.s isr.s

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS_C:.c=.o) $(SRCS_S:.s=.o))

# Build all objects
all: $(OBJS)

# C source compilation
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      kernel/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

# Assembly source compilation
$(BUILD_DIR)/%.o: %.s
	@echo "  AS      kernel/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all