# ============================================================================
# Video Driver Module Makefile
# ============================================================================

# Source files
SRCS = vbe.c fb.c font8x16.c gfxcon.c video.c

# Object files (in build directory)
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

# Build all objects
all: $(OBJS)

# Compilation rule
$(BUILD_DIR)/%.o: %.c
	@echo "  CC      driver/video/$<"
	@$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

.PHONY: all
