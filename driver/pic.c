#include "driver/pic.h"
#include "kernel/asm.h"

/* =========================================================================
 * 8259A PIC driver
 * ========================================================================= */

void pic_init(uint8_t master_offset, uint8_t slave_offset)
{
    /* Save current masks */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* ICW1: start initialisation sequence (cascade, edge-triggered) */
    outb(PIC1_CMD,  ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD,  ICW1_INIT | ICW1_ICW4);
    io_wait();

    /* ICW2: vector offsets */
    outb(PIC1_DATA, master_offset);
    io_wait();
    outb(PIC2_DATA, slave_offset);
    io_wait();

    /* ICW3: cascade wiring
     *   master: bit 2 set  → IRQ2 line has a slave attached
     *   slave:  value 2    → slave is connected to master IRQ2 */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* ICW4: 8086/88 mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Restore saved masks (or mask all if you prefer a clean slate) */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
    pic_disable_all();
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);   /* slave must be acknowledged first */
    outb(PIC1_CMD, PIC_EOI);
}

void pic_enable_irq(uint8_t irq)
{
    if (irq < 8) {
        uint8_t mask = inb(PIC1_DATA);
        mask &= (uint8_t)~(1u << irq);
        outb(PIC1_DATA, mask);
    } else {
        uint8_t mask = inb(PIC2_DATA);
        mask &= (uint8_t)~(1u << (irq - 8));
        outb(PIC2_DATA, mask);
    }
}

void pic_disable_irq(uint8_t irq)
{
    if (irq < 8) {
        uint8_t mask = inb(PIC1_DATA);
        mask |= (uint8_t)(1u << irq);
        outb(PIC1_DATA, mask);
    } else {
        uint8_t mask = inb(PIC2_DATA);
        mask |= (uint8_t)(1u << (irq - 8));
        outb(PIC2_DATA, mask);
    }
}

void pic_disable_all(void)
{
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
