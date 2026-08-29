# ============================================================================
# Memory Management Module Makefile
# ============================================================================

# Source files (arch-specific paging/memory detection lives in arch/)
SRCS = mminit.c buddy.c slab.c vmalloc.c vma.c pagefault.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      mm/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all