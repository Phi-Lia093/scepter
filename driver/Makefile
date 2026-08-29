# ============================================================================
# Driver Module Makefile (Orchestrator)
# ============================================================================

# Subdirectories (interrupt controllers + timer live in arch/)
SUBDIRS = pci char block acpi video net

# Build all drivers
all: subdirs

# Build subdirectories
subdirs:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir || exit 1; \
	done

.PHONY: all subdirs