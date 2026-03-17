#ifndef ACPI_TABLES_H
#define ACPI_TABLES_H

#include "driver/acpi/acpi.h"
#include <stdint.h>

/* ============================================================================
 * ACPI Table Structures
 * ============================================================================ */

/* ============================================================================
 * FADT - Fixed ACPI Description Table (Signature: "FACP")
 * ============================================================================ */

typedef struct {
    acpi_sdt_header_t header;
    uint32_t firmware_ctrl;     /* Physical address of FACS */
    uint32_t dsdt;              /* Physical address of DSDT */
    uint8_t reserved;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;           /* System Control Interrupt */
    uint32_t smi_cmd;           /* Port address of SMI command port */
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;      /* Port address of PM1a Event Register Block */
    uint32_t pm1b_evt_blk;      /* Port address of PM1b Event Register Block */
    uint32_t pm1a_cnt_blk;      /* Port address of PM1a Control Register Block */
    uint32_t pm1b_cnt_blk;      /* Port address of PM1b Control Register Block */
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved2;
    uint32_t flags;
    /* ACPI 2.0+ fields follow, not needed for basic shutdown */
} __attribute__((packed)) acpi_fadt_t;

/* PM1 Control Register bits */
#define ACPI_PM1_SCI_EN     (1 << 0)    /* Enable SCI interrupt */
#define ACPI_PM1_BM_RLD     (1 << 1)    /* Bus master reload */
#define ACPI_PM1_GBL_RLS    (1 << 2)    /* Global release */
#define ACPI_PM1_SLP_TYP(x) ((x) << 10) /* Sleep type (3 bits) */
#define ACPI_PM1_SLP_EN     (1 << 13)   /* Sleep enable */

/* ============================================================================
 * MADT - Multiple APIC Description Table (Signature: "APIC")
 * ============================================================================ */

typedef struct {
    acpi_sdt_header_t header;
    uint32_t local_apic_address;    /* Physical address of local APIC */
    uint32_t flags;                 /* Multiple APIC flags */
    /* Variable-length entries follow */
} __attribute__((packed)) acpi_madt_t;

/* MADT flags */
#define ACPI_MADT_PCAT_COMPAT  (1 << 0)  /* System has legacy 8259 PICs */

/* MADT Entry Types */
#define ACPI_MADT_TYPE_LOCAL_APIC           0
#define ACPI_MADT_TYPE_IO_APIC              1
#define ACPI_MADT_TYPE_INTERRUPT_OVERRIDE   2
#define ACPI_MADT_TYPE_NMI_SOURCE           3
#define ACPI_MADT_TYPE_LOCAL_APIC_NMI       4
#define ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE  5
#define ACPI_MADT_TYPE_IO_SAPIC             6
#define ACPI_MADT_TYPE_LOCAL_SAPIC          7
#define ACPI_MADT_TYPE_PLATFORM_INT_SRC     8
#define ACPI_MADT_TYPE_LOCAL_X2APIC         9
#define ACPI_MADT_TYPE_LOCAL_X2APIC_NMI    10

/* MADT Entry Header */
typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) acpi_madt_entry_header_t;

/* Processor Local APIC */
typedef struct {
    acpi_madt_entry_header_t header;
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;                 /* 1 = enabled */
} __attribute__((packed)) acpi_madt_local_apic_t;

/* I/O APIC */
typedef struct {
    acpi_madt_entry_header_t header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;       /* Physical address */
    uint32_t global_system_interrupt_base;
} __attribute__((packed)) acpi_madt_io_apic_t;

/* Interrupt Source Override */
typedef struct {
    acpi_madt_entry_header_t header;
    uint8_t bus;                    /* 0 = ISA */
    uint8_t source;                 /* Bus-relative interrupt source (IRQ) */
    uint32_t global_system_interrupt;
    uint16_t flags;                 /* MPS INTI flags */
} __attribute__((packed)) acpi_madt_interrupt_override_t;

/* ============================================================================
 * Table Parsing Functions
 * ============================================================================ */

/**
 * Find a table by signature in RSDT
 * @param rsdt Pointer to RSDT
 * @param signature 4-character signature (e.g., "FACP", "APIC")
 * @return Physical address of table, or 0 if not found
 */
uint32_t acpi_find_table(acpi_rsdt_t *rsdt, const char *signature);

/**
 * Validate ACPI table checksum
 * @param table Pointer to table
 * @param length Length of table
 * @return 1 if valid, 0 if invalid
 */
int acpi_validate_checksum(void *table, uint32_t length);

#endif /* ACPI_TABLES_H */