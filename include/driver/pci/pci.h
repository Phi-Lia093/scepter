#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/* ============================================================================
 * PCI (Peripheral Component Interconnect) Driver
 *
 * Provides PCI configuration space access and device enumeration.
 * Supports PCI Local Bus Specification 2.x/3.x.
 * ============================================================================ */

/* PCI Configuration Space Access Ports */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

/* PCI Configuration Space Register Offsets */
#define PCI_REG_VENDOR_ID       0x00  /* Vendor ID (16-bit) */
#define PCI_REG_DEVICE_ID       0x02  /* Device ID (16-bit) */
#define PCI_REG_COMMAND         0x04  /* Command Register (16-bit) */
#define PCI_REG_STATUS          0x06  /* Status Register (16-bit) */
#define PCI_REG_REVISION_ID     0x08  /* Revision ID (8-bit) */
#define PCI_REG_PROG_IF         0x09  /* Programming Interface (8-bit) */
#define PCI_REG_SUBCLASS        0x0A  /* Subclass Code (8-bit) */
#define PCI_REG_CLASS_CODE      0x0B  /* Class Code (8-bit) */
#define PCI_REG_CACHE_LINE_SIZE 0x0C  /* Cache Line Size (8-bit) */
#define PCI_REG_LATENCY_TIMER   0x0D  /* Latency Timer (8-bit) */
#define PCI_REG_HEADER_TYPE     0x0E  /* Header Type (8-bit) */
#define PCI_REG_BIST            0x0F  /* Built-in Self Test (8-bit) */
#define PCI_REG_BAR0            0x10  /* Base Address Register 0 */
#define PCI_REG_BAR1            0x14  /* Base Address Register 1 */
#define PCI_REG_BAR2            0x18  /* Base Address Register 2 */
#define PCI_REG_BAR3            0x1C  /* Base Address Register 3 */
#define PCI_REG_BAR4            0x20  /* Base Address Register 4 */
#define PCI_REG_BAR5            0x24  /* Base Address Register 5 */
#define PCI_REG_SUBSYSTEM_VID   0x2C  /* Subsystem Vendor ID (16-bit) */
#define PCI_REG_SUBSYSTEM_ID    0x2E  /* Subsystem ID (16-bit) */
#define PCI_REG_INTERRUPT_LINE  0x3C  /* Interrupt Line (8-bit) */
#define PCI_REG_INTERRUPT_PIN   0x3D  /* Interrupt PIN (8-bit) */

/* PCI Command Register Bits */
#define PCI_COMMAND_IO          0x0001  /* Enable response to I/O space */
#define PCI_COMMAND_MEMORY      0x0002  /* Enable response to memory space */
#define PCI_COMMAND_MASTER      0x0004  /* Enable bus mastering */

/* PCI Class Codes */
#define PCI_CLASS_STORAGE       0x01  /* Mass Storage Controller */
#define PCI_CLASS_NETWORK       0x02  /* Network Controller */
#define PCI_CLASS_DISPLAY       0x03  /* Display Controller */
#define PCI_CLASS_BRIDGE        0x06  /* Bridge Device */

/* PCI Storage Subclass Codes */
#define PCI_SUBCLASS_SCSI       0x00  /* SCSI Bus Controller */
#define PCI_SUBCLASS_IDE        0x01  /* IDE Controller */
#define PCI_SUBCLASS_FLOPPY     0x02  /* Floppy Disk Controller */
#define PCI_SUBCLASS_RAID       0x04  /* RAID Controller */
#define PCI_SUBCLASS_ATA        0x05  /* ATA Controller */
#define PCI_SUBCLASS_SATA       0x06  /* SATA Controller */

/* PCI IDE Programming Interface Bits */
#define PCI_IDE_PRIMARY_NATIVE  0x01  /* Primary channel in native mode */
#define PCI_IDE_PRIMARY_SWITCH  0x02  /* Primary can switch modes */
#define PCI_IDE_SECONDARY_NATIVE 0x04 /* Secondary channel in native mode */
#define PCI_IDE_SECONDARY_SWITCH 0x08 /* Secondary can switch modes */
#define PCI_IDE_BUS_MASTER      0x80  /* Bus master capable */

/* BAR Types */
#define PCI_BAR_IO              0x01  /* I/O Space BAR */
#define PCI_BAR_MEMORY          0x00  /* Memory Space BAR */

/* ============================================================================
 * PCI Device Structure
 * ============================================================================ */

typedef struct {
    uint8_t bus;                /* PCI bus number (0-255) */
    uint8_t slot;               /* PCI slot number (0-31) */
    uint8_t func;               /* PCI function number (0-7) */
    uint16_t vendor_id;         /* Vendor ID */
    uint16_t device_id;         /* Device ID */
    uint8_t class_code;         /* Class code */
    uint8_t subclass;           /* Subclass code */
    uint8_t prog_if;            /* Programming interface */
    uint8_t revision_id;        /* Revision ID */
    uint32_t bar[6];            /* Base Address Registers (raw values) */
    uint8_t interrupt_line;     /* Interrupt line */
    uint8_t interrupt_pin;      /* Interrupt pin */
} pci_device_t;

/* ============================================================================
 * PCI Function Prototypes
 * ============================================================================ */

/**
 * Initialize PCI subsystem
 */
void pci_init(void);

/**
 * Read 32-bit value from PCI configuration space
 * @param bus PCI bus number
 * @param slot PCI slot number
 * @param func PCI function number
 * @param offset Configuration space offset (must be 4-byte aligned)
 * @return 32-bit value from configuration space
 */
uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/**
 * Write 32-bit value to PCI configuration space
 * @param bus PCI bus number
 * @param slot PCI slot number
 * @param func PCI function number
 * @param offset Configuration space offset (must be 4-byte aligned)
 * @param value Value to write
 */
void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

/**
 * Read 16-bit value from PCI configuration space
 */
uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/**
 * Read 8-bit value from PCI configuration space
 */
uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/**
 * Check if PCI device exists at given location
 * @return 1 if device exists, 0 otherwise
 */
int pci_device_exists(uint8_t bus, uint8_t slot, uint8_t func);

/**
 * Read PCI device information
 * @param bus PCI bus number
 * @param slot PCI slot number
 * @param func PCI function number
 * @param device Pointer to pci_device_t to fill
 * @return 0 on success, -1 if no device
 */
int pci_read_device(uint8_t bus, uint8_t slot, uint8_t func, pci_device_t *device);

/**
 * Scan PCI bus for devices matching class/subclass
 * @param class_code Class code to match (0xFF = any)
 * @param subclass Subclass to match (0xFF = any)
 * @param callback Callback function for each matching device
 */
void pci_scan_devices(uint8_t class_code, uint8_t subclass, 
                      void (*callback)(pci_device_t *device));

/**
 * Enable PCI command bits (I/O, Memory, Bus Master)
 */
void pci_enable_device(uint8_t bus, uint8_t slot, uint8_t func, uint16_t command_bits);

#endif /* PCI_H */