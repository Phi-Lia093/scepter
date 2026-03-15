/* ============================================================================
 * Serial Port Driver (COM1) - Output Only
 * ============================================================================ */

#include <stddef.h>
#include "driver/char/char.h"
#include "lib/printk.h"
#include "kernel/asm.h"

/* COM1 base port */
#define COM1_BASE 0x3F8

/* Serial port registers (offset from base) */
#define SERIAL_DATA       0  /* Data register (R/W) */
#define SERIAL_INT_EN     1  /* Interrupt enable (W) */
#define SERIAL_FIFO_CTRL  2  /* FIFO control (W) */
#define SERIAL_LINE_CTRL  3  /* Line control (W) */
#define SERIAL_MODEM_CTRL 4  /* Modem control (W) */
#define SERIAL_LINE_STATUS 5 /* Line status (R) */

/* Line status bits */
#define LSR_DATA_READY    0x01
#define LSR_TX_EMPTY      0x20

/**
 * Check if transmit buffer is empty
 */
static int serial_tx_ready(void)
{
    return inb(COM1_BASE + SERIAL_LINE_STATUS) & LSR_TX_EMPTY;
}

/**
 * Write a byte to serial port
 */
static void serial_write_byte(uint8_t byte)
{
    /* Wait for transmit buffer to be empty */
    while (!serial_tx_ready());
    
    /* Send byte */
    outb(COM1_BASE + SERIAL_DATA, byte);
}

/**
 * Write a single character (for printk)
 */
void serial_write_char(char c)
{
    /* Convert LF to CRLF for proper line endings */
    if (c == '\n') {
        serial_write_byte('\r');
    }
    serial_write_byte((uint8_t)c);
}

/**
 * Write one character (char driver interface)
 */
static int serial_write(int scnd_id, char c)
{
    (void)scnd_id;
    serial_write_char(c);
    return 0;
}

/**
 * Character device operations
 */
static char_ops_t serial_ops = {
    .read  = NULL,  /* Output only */
    .write = serial_write,
    .ioctl = NULL,
};

/* Serial device ID (character device #3) */
#define SERIAL_DEVICE_ID 3

/**
 * Initialize serial port
 */
void serial_init(void)
{
    /* Disable interrupts */
    outb(COM1_BASE + SERIAL_INT_EN, 0x00);
    
    /* Enable DLAB (Divisor Latch Access Bit) to set baud rate */
    outb(COM1_BASE + SERIAL_LINE_CTRL, 0x80);
    
    /* Set baud rate to 115200 (divisor = 1) */
    outb(COM1_BASE + SERIAL_DATA, 0x01);      /* Low byte */
    outb(COM1_BASE + SERIAL_INT_EN, 0x00);    /* High byte */
    
    /* 8 bits, no parity, 1 stop bit (0x03) */
    outb(COM1_BASE + SERIAL_LINE_CTRL, 0x03);
    
    /* Enable FIFO, clear buffers, 14-byte threshold */
    outb(COM1_BASE + SERIAL_FIFO_CTRL, 0xC7);
    
    /* Enable DTR, RTS, and OUT2 */
    outb(COM1_BASE + SERIAL_MODEM_CTRL, 0x0B);
    
    /* Register as character device #3 */
    if (register_char_device(SERIAL_DEVICE_ID, &serial_ops) < 0) {
        printk("[serial] Failed to register device\n");
        return;
    }
    
    printk("[serial] COM1 serial port initialized (115200 baud, 8N1)\n");
}
