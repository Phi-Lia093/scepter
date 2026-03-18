/* ============================================================================
 * PCI (Peripheral Component Interconnect) Driver
 * ============================================================================ */

#include "driver/pci/pci.h"
#include "kernel/asm.h"
#include "lib/printk.h"

/* ============================================================================
 * PCI Configuration Space Access
 * ============================================================================ */

/**
 * Build PCI configuration address
 */
static inline uint32_t pci_make_address(uint8_t bus, uint8_t slot, 
                                        uint8_t func, uint8_t offset)
{
    return (1U << 31) |              /* Enable bit */
           ((uint32_t)bus << 16) |   /* Bus number */
           ((uint32_t)slot << 11) |  /* Device/slot number */
           ((uint32_t)func << 8) |   /* Function number */
           (offset & 0xFC);          /* Register offset (4-byte aligned) */
}

uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address = pci_make_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write_dword(uint8_t bus, uint8_t slot, uint8_t func, 
                             uint8_t offset, uint32_t value)
{
    uint32_t address = pci_make_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t dword = pci_config_read_dword(bus, slot, func, offset & 0xFC);
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t dword = pci_config_read_dword(bus, slot, func, offset & 0xFC);
    return (uint8_t)((dword >> ((offset & 3) * 8)) & 0xFF);
}

/* ============================================================================
 * PCI Device Detection
 * ============================================================================ */

int pci_device_exists(uint8_t bus, uint8_t slot, uint8_t func)
{
    uint16_t vendor_id = pci_config_read_word(bus, slot, func, PCI_REG_VENDOR_ID);
    return (vendor_id != 0xFFFF && vendor_id != 0x0000);
}

int pci_read_device(uint8_t bus, uint8_t slot, uint8_t func, pci_device_t *device)
{
    if (!pci_device_exists(bus, slot, func)) {
        return -1;
    }
    
    device->bus = bus;
    device->slot = slot;
    device->func = func;
    
    /* Read vendor/device ID */
    uint32_t vendor_device = pci_config_read_dword(bus, slot, func, PCI_REG_VENDOR_ID);
    device->vendor_id = vendor_device & 0xFFFF;
    device->device_id = (vendor_device >> 16) & 0xFFFF;
    
    /* Read class/subclass/prog_if/revision */
    uint32_t class_info = pci_config_read_dword(bus, slot, func, PCI_REG_REVISION_ID);
    device->revision_id = class_info & 0xFF;
    device->prog_if = (class_info >> 8) & 0xFF;
    device->subclass = (class_info >> 16) & 0xFF;
    device->class_code = (class_info >> 24) & 0xFF;
    
    /* Read all 6 BARs */
    for (int i = 0; i < 6; i++) {
        device->bar[i] = pci_config_read_dword(bus, slot, func, PCI_REG_BAR0 + (i * 4));
    }
    
    /* Read interrupt info */
    device->interrupt_line = pci_config_read_byte(bus, slot, func, PCI_REG_INTERRUPT_LINE);
    device->interrupt_pin = pci_config_read_byte(bus, slot, func, PCI_REG_INTERRUPT_PIN);
    
    return 0;
}

/* ============================================================================
 * PCI Bus Scanning
 * ============================================================================ */

void pci_scan_devices(uint8_t class_code, uint8_t subclass,
                      void (*callback)(pci_device_t *device))
{
    pci_device_t device;
    
    /* Scan all buses */
    for (uint16_t bus = 0; bus < 256; bus++) {
        /* Scan all slots */
        for (uint8_t slot = 0; slot < 32; slot++) {
            /* Check function 0 first */
            if (!pci_device_exists(bus, slot, 0)) {
                continue;
            }
            
            /* Read header type to check for multi-function device */
            uint8_t header_type = pci_config_read_byte(bus, slot, 0, PCI_REG_HEADER_TYPE);
            uint8_t func_count = (header_type & 0x80) ? 8 : 1;  /* Multi-function? */
            
            /* Scan functions */
            for (uint8_t func = 0; func < func_count; func++) {
                if (pci_read_device(bus, slot, func, &device) != 0) {
                    continue;
                }
                
                /* Check if matches requested class/subclass */
                if ((class_code == 0xFF || device.class_code == class_code) &&
                    (subclass == 0xFF || device.subclass == subclass)) {
                    callback(&device);
                }
            }
        }
    }
}

/* ============================================================================
 * PCI Device Control
 * ============================================================================ */

void pci_enable_device(uint8_t bus, uint8_t slot, uint8_t func, uint16_t command_bits)
{
    uint16_t current = pci_config_read_word(bus, slot, func, PCI_REG_COMMAND);
    uint16_t new_command = current | command_bits;
    
    if (new_command != current) {
        /* Write back only if changed */
        uint32_t dword = pci_config_read_dword(bus, slot, func, PCI_REG_COMMAND);
        dword = (dword & 0xFFFF0000) | new_command;
        pci_config_write_dword(bus, slot, func, PCI_REG_COMMAND, dword);
    }
}

/* ============================================================================
 * PCI Initialization
 * ============================================================================ */

static void pci_print_device(pci_device_t *device)
{
    static const char *class_names[] = {
        "Unclassified", "Storage", "Network", "Display",
        "Multimedia", "Memory", "Bridge", "Simple Comm",
        "Base System", "Input", "Docking", "Processor",
        "Serial Bus", "Wireless", "Intelligent", "Satellite",
        "Encryption", "Signal Processing"
    };
    
    const char *class_name = (device->class_code < 18) ? 
                             class_names[device->class_code] : "Unknown";
    
    printk("[PCI] %02x:%02x.%x %s (%02x:%02x) - %04x:%04x\n",
           device->bus, device->slot, device->func,
           class_name, device->class_code, device->subclass,
           device->vendor_id, device->device_id);
}

void pci_init(void)
{
    printk("[PCI] Initializing PCI subsystem...\n");
    
    /* Test if PCI is available by checking for host bridge */
    if (!pci_device_exists(0, 0, 0)) {
        printk("[PCI] No PCI host bridge found - PCI not available\n");
        return;
    }
    
    printk("[PCI] Scanning PCI bus...\n");
    
    /* Scan and print all devices */
    pci_scan_devices(0xFF, 0xFF, pci_print_device);
    
    printk("[PCI] PCI initialization complete\n");
}