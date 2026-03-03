#include "kernel/cpu.h"
#include <stddef.h>

/* =========================================================================
 * GDT
 * ========================================================================= */

#define GDT_ENTRIES 5

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;

/* Fill one GDT entry with a flat segment descriptor */
static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t gran)
{
    gdt[idx].base_low    = (base  & 0x0000FFFF);
    gdt[idx].base_mid    = (base  & 0x00FF0000) >> 16;
    gdt[idx].base_high   = (base  & 0xFF000000) >> 24;

    gdt[idx].limit_low   = (limit & 0x0000FFFF);
    gdt[idx].granularity = (limit & 0x000F0000) >> 16;  /* limit_high nibble */

    gdt[idx].granularity |= gran & 0xF0;  /* G | DB | L | AVL */
    gdt[idx].access       = access;
}

void gdt_init(void)
{
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    /*
     * access byte layout: P | DPL(2) | S | Type(4)
     *   0x9A = 1 001 1 010 -> P=1, DPL=0, S=1 (code/data), Type=1010 (code, readable)
     *   0x92 = 1 001 1 010 -> P=1, DPL=0, S=1,             Type=0010 (data, writable)
     *   0xFA = 1 111 1 010 -> P=1, DPL=3, S=1,             Type=1010 (code, readable)
     *   0xF2 = 1 111 1 010 -> P=1, DPL=3, S=1,             Type=0010 (data, writable)
     *
     * granularity byte: G | DB | L | AVL | limit_high
     *   0xCF = 1100 1111 -> G=1 (4KB), DB=1 (32-bit), L=0, AVL=0, limit_high=0xF
     */

    /* 0: null descriptor */
    gdt_set_entry(0, 0, 0, 0x00, 0x00);

    /* 1: kernel code  - selector 0x08 */
    gdt_set_entry(1, 0x00000000, 0xFFFFFFFF, 0x9A, 0xCF);

    /* 2: kernel data  - selector 0x10 */
    gdt_set_entry(2, 0x00000000, 0xFFFFFFFF, 0x92, 0xCF);

    /* 3: user code    - selector 0x18 (DPL=3) */
    gdt_set_entry(3, 0x00000000, 0xFFFFFFFF, 0xFA, 0xCF);

    /* 4: user data    - selector 0x20 (DPL=3) */
    gdt_set_entry(4, 0x00000000, 0xFFFFFFFF, 0xF2, 0xCF);

    gdt_flush(&gdt_ptr);
}

/* =========================================================================
 * IDT
 * ========================================================================= */

#define IDT_ENTRIES 256

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idt_ptr;

void idt_set_gate(uint8_t num, uint32_t handler, uint16_t sel, uint8_t flags)
{
    idt[num].offset_low  = (handler & 0x0000FFFF);
    idt[num].offset_high = (handler & 0xFFFF0000) >> 16;
    idt[num].selector    = sel;
    idt[num].zero        = 0;
    idt[num].type_attr   = flags;
}

void idt_init(void)
{
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    /* Zero all entries */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    idt_flush(&idt_ptr);
}

void isr_init(void)
{
    idt_set_gate( 0, (uint32_t)isr0,  GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate( 1, (uint32_t)isr1,  GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate( 2, (uint32_t)isr2,  GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate( 3, (uint32_t)isr3,  GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate( 4, (uint32_t)isr4,  GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate( 5, (uint32_t)isr5,  GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate( 6, (uint32_t)isr6,  GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate( 7, (uint32_t)isr7,  GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate( 8, (uint32_t)isr8,  GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate( 9, (uint32_t)isr9,  GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(10, (uint32_t)isr10, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(11, (uint32_t)isr11, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(12, (uint32_t)isr12, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(13, (uint32_t)isr13, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(14, (uint32_t)isr14, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(15, (uint32_t)isr15, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(16, (uint32_t)isr16, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(17, (uint32_t)isr17, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(18, (uint32_t)isr18, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(19, (uint32_t)isr19, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(20, (uint32_t)isr20, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(21, (uint32_t)isr21, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(22, (uint32_t)isr22, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(23, (uint32_t)isr23, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(24, (uint32_t)isr24, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(25, (uint32_t)isr25, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(26, (uint32_t)isr26, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(27, (uint32_t)isr27, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(28, (uint32_t)isr28, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(29, (uint32_t)isr29, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(30, (uint32_t)isr30, GDT_KERNEL_CODE, IDT_GATE_INT32);
    idt_set_gate(31, (uint32_t)isr31, GDT_KERNEL_CODE, IDT_GATE_INT32);
}

/* =========================================================================
 * Paging
 * ========================================================================= */
// global var for page physical addr
volatile uint32_t kernel_page_table;