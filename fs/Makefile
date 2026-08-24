# ============================================================================
# Filesystem Module Makefile
# ============================================================================

# Subdirectories
SUBDIRS = minix3

# Source files in fs/
SRCS = vfs.c devfs.c pipe.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS) subdirs

# Build subdirectories
subdirs:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      fs/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all subdirs
