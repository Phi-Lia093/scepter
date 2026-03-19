#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include "kernel/asm.h"

/* =========================================================================
 * GDT
 * ========================================================================= */

typedef struct {
    uint16_t limit_low;    /* bits 15:0  of segment limit */
    uint16_t base_low;     /* bits 15:0  of base address  */
    uint8_t  base_mid;     /* bits 23:16 of base address  */
    uint8_t  access;       /* P | DPL(2) | S | Type(4)    */
    uint8_t  granularity;  /* G | DB | L | AVL | limit_high(4) */
    uint8_t  base_high;    /* bits 31:24 of base address  */
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;        /* size of GDT - 1 */
    uint32_t base;         /* linear address of GDT */
} __attribute__((packed)) gdt_ptr_t;

/* GDT segment selectors */
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10
#define GDT_USER_CODE    0x18
#define GDT_USER_DATA    0x20
#define GDT_TSS          0x28

/* =========================================================================
 * TSS (Task State Segment)
 * ========================================================================= */

typedef struct {
    uint32_t prev_tss;   /* Previous TSS (unused) */
    uint32_t esp0;       /* Stack pointer for ring 0 (kernel) */
    uint32_t ss0;        /* Stack segment for ring 0 (kernel) */
    uint32_t esp1;       /* Ring 1 (unused) */
    uint32_t ss1;
    uint32_t esp2;       /* Ring 2 (unused) */
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

/* Global TSS - used to set esp0 for ring 3->0 transitions */
extern tss_entry_t tss;

void tss_init(void);

/* Load GDT and reload all segment registers (inline asm) */
static inline void gdt_flush(gdt_ptr_t *ptr)
{
    __asm__ volatile (
        "lgdt  (%0)          \n"
        "movw  $0x10, %%ax   \n"   /* kernel data selector */
        "movw  %%ax,  %%ds   \n"
        "movw  %%ax,  %%es   \n"
        "movw  %%ax,  %%fs   \n"
        "movw  %%ax,  %%gs   \n"
        "movw  %%ax,  %%ss   \n"
        /* far-return trick: push CS:EIP then lret to reload CS */
        "pushl $0x08         \n"
        "pushl $1f           \n"
        "lret                \n"
        "1:                  \n"
        :
        : "r"(ptr)
        : "eax", "memory"
    );
}

void gdt_init(void);

/* =========================================================================
 * ISR stubs (defined in kernel/isr.s)
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

/* Register all 32 exception stubs in the IDT */
void isr_init(void);

/* IRQ stubs (defined in kernel/isr.s) */
extern void irq0(void);   /* PIT timer */

/* =========================================================================
 * IDT
 * ========================================================================= */

typedef struct {
    uint16_t offset_low;   /* bits 15:0  of handler address */
    uint16_t selector;     /* code segment selector         */
    uint8_t  zero;         /* always 0                      */
    uint8_t  type_attr;    /* P | DPL(2) | 0 | Type(4)     */
    uint16_t offset_high;  /* bits 31:16 of handler address */
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;        /* size of IDT - 1 */
    uint32_t base;         /* linear address of IDT */
} __attribute__((packed)) idt_ptr_t;

/* IDT gate type_attr flags */
#define IDT_GATE_INT32   0x8E  /* P=1, DPL=0, type=interrupt gate (32-bit) */
#define IDT_GATE_TRAP32  0x8F  /* P=1, DPL=0, type=trap gate     (32-bit) */
#define IDT_GATE_USER    0xEE  /* P=1, DPL=3, type=interrupt gate (32-bit) */

/* Load IDT (inline asm) */
static inline void idt_flush(idt_ptr_t *ptr)
{
    __asm__ volatile ("lidt (%0)" : : "r"(ptr) : "memory");
}

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t handler, uint16_t sel, uint8_t flags);

#endif /* CPU_H */
