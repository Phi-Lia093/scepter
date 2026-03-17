#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

/* ============================================================================
 * ACPI (Advanced Configuration and Power Interface)
 *
 * Provides system power management and hardware configuration discovery.
 * Implements ACPI 1.0 specification features:
 *   - RSDP discovery
 *   - Table parsing (RSDT, FADT, MADT)
 *   - System shutdown
 *   - Device enumeration
 * ============================================================================ */

/* ACPI Signature type (4 bytes) */
typedef char acpi_sig_t[4];

/* ============================================================================
 * RSDP - Root System Description Pointer
 * ============================================================================ */

typedef struct {
    char signature[8];          /* "RSD PTR " (with trailing space) */
    uint8_t checksum;           /* Checksum of first 20 bytes */
    char oem_id[6];             /* OEM identifier */
    uint8_t revision;           /* 0 = ACPI 1.0, 2 = ACPI 2.0+ */
    uint32_t rsdt_address;      /* Physical address of RSDT */
} __attribute__((packed)) acpi_rsdp_t;

/* ACPI 2.0+ Extended RSDP */
typedef struct {
    acpi_rsdp_t rsdp;           /* ACPI 1.0 RSDP */
    uint32_t length;            /* Length of entire table */
    uint64_t xsdt_address;      /* 64-bit physical address of XSDT */
    uint8_t extended_checksum;  /* Checksum of entire table */
    uint8_t reserved[3];
} __attribute__((packed)) acpi_xrsdp_t;

/* ============================================================================
 * SDT Header - Common to all ACPI tables
 * ============================================================================ */

typedef struct {
    acpi_sig_t signature;       /* Table signature (e.g., "FACP", "APIC") */
    uint32_t length;            /* Length of entire table including header */
    uint8_t revision;           /* Revision of table structure */
    uint8_t checksum;           /* Checksum of entire table */
    char oem_id[6];             /* OEM identifier */
    char oem_table_id[8];       /* OEM table identifier */
    uint32_t oem_revision;      /* OEM revision */
    uint32_t creator_id;        /* Vendor ID of utility that created table */
    uint32_t creator_revision;  /* Revision of utility */
} __attribute__((packed)) acpi_sdt_header_t;

/* ============================================================================
 * RSDT - Root System Description Table
 * ============================================================================ */

typedef struct {
    acpi_sdt_header_t header;   /* Signature: "RSDT" */
    uint32_t entries[];         /* Array of physical addresses to other SDTs */
} __attribute__((packed)) acpi_rsdt_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Initialize ACPI subsystem
 * - Discovers RSDP
 * - Parses RSDT/XSDT
 * - Locates FADT and MADT
 * - Prepares for shutdown and device enumeration
 */
void acpi_init(void);

/**
 * Shutdown the system via ACPI
 * Uses PM1a control register from FADT
 */
void acpi_shutdown(void);

/**
 * Reboot the system
 * Uses ACPI reset register if available, falls back to keyboard controller
 */
void acpi_reboot(void);

/**
 * Enumerate devices from ACPI tables
 * Prints information about CPUs, I/O APICs, and other devices
 */
void acpi_enum_devices(void);

/**
 * Check if ACPI is available
 * @return 1 if ACPI initialized successfully, 0 otherwise
 */
int acpi_is_available(void);

#endif /* ACPI_H */