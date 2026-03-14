# ============================================================================
# Driver Module Makefile (Orchestrator)
# ============================================================================

# Subdirectories
SUBDIRS = char block

# Root driver files (pic.c in driver/)
SRCS = pic.c
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all drivers
all: $(OBJS) subdirs

# Build subdirectories
subdirs:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

# Compilation rule for root driver files
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      driver/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all subdirs