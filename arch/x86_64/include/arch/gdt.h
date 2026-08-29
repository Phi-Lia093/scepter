#ifndef ARCH_X86_64_GDT_H
#define ARCH_X86_64_GDT_H

#include <stdint.h>

/* =========================================================================
 * PRIVATE x86_64 header – GDT / TSS / IDT layout.
 * Only included by arch/x86_64/ sources.
 * ========================================================================= */

/* =========================================================================
 * GDT (long mode)
 * ========================================================================= */

#define GDT_ENTRIES 7   /* null, kcode, kdata, udata, ucode, tss-lo, tss-hi */

typedef struct {
    uint16_t limit_lo;      /* bits 15:0  of segment limit   */
    uint16_t base_lo;       /* bits 15:0  of base address    */
    uint8_t  base_mid;      /* bits 23:16 of base address    */
    uint8_t  access;        /* P | DPL(2) | S | Type(4)      */
    uint8_t  flags_limit_hi;/* G | DB | L | AVL | limit(4)  */
    uint8_t  base_hi;       /* bits 31:24 of base address    */
} __attribute__((packed)) gdt_entry_t;

/* 16-byte TSS descriptor (long mode) */
typedef struct {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_hi;
    uint8_t  base_hi;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed)) gdt_tss_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_DATA    0x18   /* before user code for sysret STAR layout */
#define GDT_USER_CODE    0x20
#define GDT_TSS          0x28

/* Access byte helpers */
#define GDT_PRESENT  0x80
#define GDT_DPL0     0x00
#define GDT_DPL3     0x60
#define GDT_SEGMENT  0x10
#define GDT_EXEC     0x08
#define GDT_RW       0x02
#define GDT_LONG_MODE 0x20   /* flags: L bit */
#define GDT_SIZE_32  0x40
#define GDT_GRANULARITY 0x80

/* =========================================================================
 * TSS (x86_64: 104 bytes, minimal fields used)
 * ========================================================================= */

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed)) tss_t;

extern tss_t tss;

static inline void lgdt_inline(gdt_ptr_t *ptr)
{
    __asm__ volatile ("lgdt (%0)" : : "r"(ptr) : "memory");
}

/* =========================================================================
 * IDT
 * ========================================================================= */

#define IDT_ENTRIES 256

typedef struct {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  ist;         /* interrupt stack table index */
    uint8_t  type_attr;   /* P | DPL(2) | 0 | type(5)   */
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

#define IDT_GATE_INT32   0x8E  /* P=1, DPL=0, interrupt gate */
#define IDT_GATE_TRAP32  0x8F  /* P=1, DPL=0, trap gate      */
#define IDT_GATE_USER    0xEE  /* P=1, DPL=3, interrupt gate */

static inline void lidt_inline(idt_ptr_t *ptr)
{
    __asm__ volatile ("lidt (%0)" : : "r"(ptr) : "memory");
}

void idt_set_gate(uint8_t num, uint64_t handler, uint16_t sel,
                  uint8_t type_attr, uint8_t ist);

/* =========================================================================
 * ISR / IRQ stubs (defined in arch/x86_64/isr.s)
 * ========================================================================= */

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

/* Syscall entry (via the syscall instruction) */
extern void syscall_entry(void);

extern void irq0(void);   extern void irq1(void);   extern void irq2(void);
extern void irq3(void);   extern void irq4(void);   extern void irq5(void);
extern void irq6(void);   extern void irq7(void);   extern void irq8(void);
extern void irq9(void);   extern void irq10(void);  extern void irq11(void);
extern void irq12(void);  extern void irq13(void);  extern void irq14(void);
extern void irq15(void);

#endif /* ARCH_X86_64_GDT_H */
