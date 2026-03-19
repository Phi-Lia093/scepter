# ============================================================================
# Userspace Programs Makefile
# ============================================================================

# Programs to build
PROGRAMS = test.bin

# Build all programs
all: $(PROGRAMS)

# Assemble flat binary
%.bin: %.s
	@echo "  NASM    userspace/$<"
	@nasm -f bin -o $@ $<

.PHONY: all clean

clean:
	@rm -f $(PROGRAMS)