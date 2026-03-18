/* ============================================================================
 * Multi-Port Serial Driver with Dynamic Discovery
 * ============================================================================ */

#include "driver/char/serial.h"
#include "driver/char/char.h"
#include "driver/pci/pci.h"
#include "driver/acpi/acpi.h"
#include "fs/devfs.h"
#include "lib/printk.h"
#include "lib/string.h"
#include "kernel/asm.h"
#include <stddef.h>

/* ============================================================================
 * Serial Port Registers
 * ============================================================================ */

/* Register offsets from base port */
#define SERIAL_DATA       0  /* Data register (R/W) */
#define SERIAL_INT_EN     1  /* Interrupt enable (W) */
#define SERIAL_FIFO_CTRL  2  /* FIFO control (W) */
#define SERIAL_LINE_CTRL  3  /* Line control (W) */
#define SERIAL_MODEM_CTRL 4  /* Modem control (W) */
#define SERIAL_LINE_STATUS 5 /* Line status (R) */
#define SERIAL_MODEM_STATUS 6 /* Modem status (R) */
#define SERIAL_SCRATCH    7  /* Scratch register (R/W) */

/* Line status bits */
#define LSR_DATA_READY    0x01
#define LSR_OVERRUN_ERR   0x02
#define LSR_PARITY_ERR    0x04
#define LSR_FRAMING_ERR   0x08
#define LSR_BREAK_INT     0x10
#define LSR_TX_EMPTY      0x20
#define LSR_TX_IDLE       0x40

/* ============================================================================
 * Global State
 * ============================================================================ */

static serial_port_t serial_ports[SERIAL_MAX_PORTS];
static int serial_port_count = 0;

/* ============================================================================
 * Low-Level Port Operations
 * ============================================================================ */

/**
 * Check if transmit buffer is empty
 */
static bool serial_tx_ready(uint16_t port)
{
    return (inb(port + SERIAL_LINE_STATUS) & LSR_TX_EMPTY) != 0;
}

/**
 * Write a byte to serial port
 */
static void serial_write_byte(uint16_t port, uint8_t byte)
{
    /* Wait for transmit buffer to be empty */
    int timeout = 100000;
    while (!serial_tx_ready(port) && timeout-- > 0);
    
    /* Send byte */
    outb(port + SERIAL_DATA, byte);
}

/**
 * Read a byte from serial port (if available)
 */
static uint8_t serial_read_byte(uint16_t port)
{
    if (inb(port + SERIAL_LINE_STATUS) & LSR_DATA_READY) {
        return inb(port + SERIAL_DATA);
    }
    return 0;
}

/* ============================================================================
 * Port Detection
 * ============================================================================ */

/**
 * Test if a serial port exists by checking scratch register
 */
static bool serial_port_exists(uint16_t port)
{
    /* Test scratch register with pattern */
    outb(port + SERIAL_SCRATCH, 0x55);
    if (inb(port + SERIAL_SCRATCH) != 0x55) return false;
    
    outb(port + SERIAL_SCRATCH, 0xAA);
    if (inb(port + SERIAL_SCRATCH) != 0xAA) return false;
    
    return true;
}

/**
 * Detect legacy ISA serial ports (COM1-COM4)
 */
static void serial_detect_legacy(void)
{
    static const uint16_t legacy_ports[] = {
        0x3F8,  /* COM1 */
        0x2F8,  /* COM2 */
        0x3E8,  /* COM3 */
        0x2E8   /* COM4 */
    };
    
    static const uint8_t legacy_irqs[] = {4, 3, 4, 3};
    
    printk("[serial] Detecting legacy ISA serial ports...\n");
    
    for (int i = 0; i < 4 && serial_port_count < SERIAL_MAX_PORTS; i++) {
        if (serial_port_exists(legacy_ports[i])) {
            serial_port_t *port = &serial_ports[serial_port_count];
            port->type = SERIAL_TYPE_ISA;
            port->io_port = legacy_ports[i];
            port->irq = legacy_irqs[i];
            port->active = false;  /* Not initialized yet */
            
            printk("[serial] Found COM%d at 0x%03x, IRQ %u\n",
                   i + 1, port->io_port, port->irq);
            
            serial_port_count++;
        }
    }
}

/**
 * PCI UART detection callback
 */
static void serial_pci_callback(pci_device_t *dev)
{
    if (serial_port_count >= SERIAL_MAX_PORTS) {
        return;  /* No more slots */
    }
    
    serial_port_t *port = &serial_ports[serial_port_count];
    
    printk("[serial] Found PCI UART: %04x:%04x\n",
           dev->vendor_id, dev->device_id);
    
    /* Enable I/O space */
    pci_enable_device(dev->bus, dev->slot, dev->func, PCI_COMMAND_IO);
    
    /* Check BAR0 for I/O port or MMIO */
    if (dev->bar[0] & PCI_BAR_IO) {
        /* I/O port based */
        port->type = SERIAL_TYPE_PCI;
        port->io_port = dev->bar[0] & 0xFFFC;
        printk("[serial]   I/O port: 0x%03x\n", port->io_port);
    } else {
        /* MMIO based (not implemented yet) */
        port->type = SERIAL_TYPE_MMIO;
        port->mmio_base = dev->bar[0] & 0xFFFFFFF0;
        printk("[serial]   MMIO base: 0x%08x (not supported yet)\n", 
               port->mmio_base);
        return;  /* Skip MMIO for now */
    }
    
    port->irq = dev->interrupt_line;
    port->active = false;
    
    serial_port_count++;
}

/* ============================================================================
 * Port Initialization
 * ============================================================================ */

/**
 * Initialize a single serial port
 */
static void serial_init_port(serial_port_t *port, int port_id)
{
    uint16_t base = port->io_port;
    
    /* Disable interrupts */
    outb(base + SERIAL_INT_EN, 0x00);
    
    /* Enable DLAB (Divisor Latch Access Bit) to set baud rate */
    outb(base + SERIAL_LINE_CTRL, 0x80);
    
    /* Set baud rate to 115200 (divisor = 1) */
    outb(base + SERIAL_DATA, 0x01);      /* Low byte */
    outb(base + SERIAL_INT_EN, 0x00);    /* High byte */
    
    /* 8 bits, no parity, 1 stop bit (8N1) */
    outb(base + SERIAL_LINE_CTRL, 0x03);
    
    /* Enable FIFO, clear buffers, 14-byte threshold */
    outb(base + SERIAL_FIFO_CTRL, 0xC7);
    
    /* Enable DTR, RTS, and OUT2 */
    outb(base + SERIAL_MODEM_CTRL, 0x0B);
    
    /* Mark as active */
    port->active = true;
    
    /* Generate device name */
    const char *names[] = {"serial0", "serial1", "serial2", "serial3"};
    if (port_id < 4) {
        strcpy(port->name, names[port_id]);
    }
    
    printk("[serial] Initialized %s at 0x%03x (115200 baud, 8N1)\n",
           port->name, base);
}

/* ============================================================================
 * Character Device Interface
 * ============================================================================ */

/**
 * Read from serial port
 */
static char serial_read(int scnd_id)
{
    if (scnd_id < 0 || scnd_id >= serial_port_count) return 0;
    
    serial_port_t *port = &serial_ports[scnd_id];
    if (!port->active) return 0;
    
    return (char)serial_read_byte(port->io_port);
}

/**
 * Write to serial port
 */
static int serial_write(int scnd_id, char c)
{
    if (scnd_id < 0 || scnd_id >= serial_port_count) return -1;
    
    serial_port_t *port = &serial_ports[scnd_id];
    if (!port->active) return -1;
    
    /* Convert LF to CRLF for proper line endings */
    if (c == '\n') {
        serial_write_byte(port->io_port, '\r');
    }
    serial_write_byte(port->io_port, (uint8_t)c);
    
    return 0;
}

/**
 * Character device operations
 */
static char_ops_t serial_ops = {
    .read  = serial_read,
    .write = serial_write,
    .ioctl = NULL,
};

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Write a character to serial0 (for kernel logging)
 */
void serial_write_char(char c)
{
    if (serial_port_count > 0 && serial_ports[0].active) {
        if (c == '\n') {
            serial_write_byte(serial_ports[0].io_port, '\r');
        }
        serial_write_byte(serial_ports[0].io_port, (uint8_t)c);
    }
}

/**
 * Get number of detected serial ports
 */
int serial_get_port_count(void)
{
    return serial_port_count;
}

/**
 * Get serial port descriptor
 */
serial_port_t* serial_get_port(int port_id)
{
    if (port_id < 0 || port_id >= serial_port_count) {
        return NULL;
    }
    return &serial_ports[port_id];
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * Initialize serial port subsystem
 */
void serial_init(void)
{
    printk("[serial] Initializing serial port subsystem...\n");
    
    serial_port_count = 0;
    
    /* 1. Scan PCI for UART controllers */
    printk("[serial] Scanning PCI for UART controllers...\n");
    pci_scan_devices(PCI_CLASS_SIMPLE_COMM, PCI_SUBCLASS_SERIAL, 
                    serial_pci_callback);
    
    /* 2. Detect legacy ISA ports */
    serial_detect_legacy();
    
    if (serial_port_count == 0) {
        printk("[serial] WARNING: No serial ports detected!\n");
        return;
    }
    
    printk("[serial] Found %d serial port(s)\n", serial_port_count);
    
    /* Initialize all detected ports and register as char devices */
    for (int i = 0; i < serial_port_count; i++) {
        serial_init_port(&serial_ports[i], i);
        
        /* Register as character device */
        if (register_char_device(SERIAL_BASE_DEVICE_ID + i, &serial_ops) == 0) {
            devfs_register_device(serial_ports[i].name, DT_CHRDEV,
                                SERIAL_BASE_DEVICE_ID + i, 0);
        } else {
            printk("[serial] Failed to register %s\n", serial_ports[i].name);
        }
    }
    
    /* serial0 is used for kernel logging */
    if (serial_port_count > 0) {
        printk("[serial] Using %s for kernel logging\n", serial_ports[0].name);
    }
}