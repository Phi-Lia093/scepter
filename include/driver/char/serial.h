#ifndef SERIAL_H
#define SERIAL_H

/**
 * Initialize COM1 serial port (output only)
 * Configures for 115200 baud, 8N1
 * Registers as /dev/serial0
 */
void serial_init(void);

#endif /* SERIAL_H */