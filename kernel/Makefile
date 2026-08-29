# ============================================================================
# Kernel Module Makefile
# ============================================================================

# Source files (arch-specific code lives in arch/, not here)
SRCS_C = kernel.c panic.c sched.c exec.c syscall.c spawn.c process.c signal.c syscall_new.c kmsg.c syscalls_net.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS_C:.c=.o))

# Build all objects
all: $(OBJS)

# C source compilation
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      kernel/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all