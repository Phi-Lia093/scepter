#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/* =========================================================================
 * 8259A Programmable Interrupt Controller
 * ========================================================================= */

/* I/O ports */
#define PIC1_CMD   0x20   /* master PIC command port */
#define PIC1_DATA  0x21   /* master PIC data / IMR   */
#define PIC2_CMD   0xA0   /* slave  PIC command port */
#define PIC2_DATA  0xA1   /* slave  PIC data / IMR   */

/* Commands */
#define PIC_EOI    0x20   /* End-Of-Interrupt        */

/* Initialisation Control Words */
#define ICW1_INIT  0x10   /* begin initialisation    */
#define ICW1_ICW4  0x01   /* ICW4 needed             */
#define ICW4_8086  0x01   /* 8086/88 mode            */

/* IRQ numbers (0 = master IRQ0, 15 = slave IRQ7) */
#define IRQ0   0    /* PIT timer          */
#define IRQ1   1    /* keyboard           */
#define IRQ2   2    /* cascade (slave)    */
#define IRQ3   3    /* COM2               */
#define IRQ4   4    /* COM1               */
#define IRQ5   5    /* LPT2 / sound       */
#define IRQ6   6    /* floppy             */
#define IRQ7   7    /* LPT1 / spurious    */
#define IRQ8   8    /* RTC                */
#define IRQ9   9    /* free / ACPI        */
#define IRQ10  10   /* free               */
#define IRQ11  11   /* free               */
#define IRQ12  12   /* PS/2 mouse         */
#define IRQ13  13   /* FPU                */
#define IRQ14  14   /* primary ATA        */
#define IRQ15  15   /* secondary ATA      */

/* =========================================================================
 * Functions
 * ========================================================================= */

/*
 * Initialise both PICs.
 * master_offset: IDT vector base for IRQ0–7  (typically 0x20 = 32)
 * slave_offset:  IDT vector base for IRQ8–15 (typically 0x28 = 40)
 * All IRQs are masked after init; enable individually with pic_enable_irq().
 */
void pic_init(uint8_t master_offset, uint8_t slave_offset);

/* Send End-Of-Interrupt for the given IRQ (0–15) */
void pic_send_eoi(uint8_t irq);

/* Unmask (enable) a single IRQ line */
void pic_enable_irq(uint8_t irq);

/* Mask (disable) a single IRQ line */
void pic_disable_irq(uint8_t irq);

/* Mask all IRQ lines on both PICs */
void pic_disable_all(void);

#endif /* PIC_H */
