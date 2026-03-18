#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Multi-Port Serial Driver
 *
 * Supports dynamic detection of serial ports via:
 * 1. ACPI SPCR (Serial Port Console Redirection) table
 * 2. PCI UART controllers (class 0x07, subclass 0x00)
 * 3. Legacy ISA ports (COM1-COM4: 0x3F8, 0x2F8, 0x3E8, 0x2E8)
 *
 * Registers up to 4 ports as serial0-serial3 in /dev
 * serial0 is used for kernel logging (printk)
 * ============================================================================ */

/* Serial port configuration */
#define SERIAL_MAX_PORTS        4       /* Maximum number of ports */
#define SERIAL_BASE_DEVICE_ID   10      /* Base char device ID (10-13) */

/* Serial port types */
typedef enum {
    SERIAL_TYPE_ISA,        /* Legacy ISA I/O port */
    SERIAL_TYPE_PCI,        /* PCI UART card (I/O) */
    SERIAL_TYPE_MMIO        /* Memory-mapped I/O */
} serial_type_t;

/* Serial port descriptor */
typedef struct {
    bool active;            /* Port is active and initialized */
    serial_type_t type;     /* Port type */
    uint16_t io_port;       /* I/O port base (for ISA/PCI I/O) */
    uint32_t mmio_base;     /* MMIO base address (for MMIO UARTs) */
    uint8_t irq;            /* IRQ number */
    char name[10];          /* "serial0", "serial1", etc. */
} serial_port_t;

/* ============================================================================
 * Function Prototypes
 * ============================================================================ */

/**
 * Initialize serial port subsystem
 * Detects all available serial ports and registers them as char devices
 */
void serial_init(void);

/**
 * Write a character to serial0 (for kernel logging)
 * @param c Character to write
 */
void serial_write_char(char c);

/**
 * Get number of detected serial ports
 * @return Number of active serial ports (0-4)
 */
int serial_get_port_count(void);

/**
 * Get serial port descriptor
 * @param port_id Port ID (0-3)
 * @return Pointer to port descriptor, or NULL if invalid
 */
serial_port_t* serial_get_port(int port_id);

#endif /* SERIAL_H */