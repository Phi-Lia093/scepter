/* ============================================================================
 * x86_64 CPU setup: GDT / TSS / IDT + syscall MSRs
 * ============================================================================ */

#include "arch/cpu.h"
#include "arch/gdt.h"
#include "arch/irq.h"
#include "arch/paging.h"
#include "arch/pic.h"
#include "arch/msr.h"
#include <stddef.h>

/* =========================================================================
 * GDT
 * ========================================================================= */

static gdt_entry_t gdt[GDT_ENTRIES] __attribute__((aligned(16)));

static void gdt_set_entry(int idx, uint8_t access, uint8_t flags)
{
    gdt[idx].limit_lo       = 0xFFFF;
    gdt[idx].base_lo        = 0;
    gdt[idx].base_mid       = 0;
    gdt[idx].access         = access;
    gdt[idx].flags_limit_hi = flags | 0x0F;
    gdt[idx].base_hi        = 0;
}

static void gdt_init(void)
{
    gdt[0] = (gdt_entry_t){0};

    /* Kernel code: P=1, DPL=0, S=1, Exec+Read, Long mode */
    gdt_set_entry(1, GDT_PRESENT | GDT_DPL0 | GDT_SEGMENT | GDT_EXEC | GDT_RW,
                  GDT_GRANULARITY | GDT_LONG_MODE);

    /* Kernel data: P=1, DPL=0, S=1, Read/Write */
    gdt_set_entry(2, GDT_PRESENT | GDT_DPL0 | GDT_SEGMENT | GDT_RW,
                  GDT_GRANULARITY | GDT_SIZE_32);

    /* User data (before user code for sysret STAR layout) */
    gdt_set_entry(3, GDT_PRESENT | GDT_DPL3 | GDT_SEGMENT | GDT_RW,
                  GDT_GRANULARITY | GDT_SIZE_32);

    /* User code: P=1, DPL=3, S=1, Exec+Read, Long mode */
    gdt_set_entry(4, GDT_PRESENT | GDT_DPL3 | GDT_SEGMENT | GDT_EXEC | GDT_RW,
                  GDT_GRANULARITY | GDT_LONG_MODE);

    gdt_ptr_t gdtr = {
        .limit = sizeof(gdt) - 1,
        .base  = (uint64_t)&gdt,
    };
    lgdt_inline(&gdtr);

    __asm__ volatile (
        "movw $0x10, %%ax \n"
        "movw %%ax, %%ds  \n"
        "movw %%ax, %%es  \n"
        "movw %%ax, %%ss  \n"
        "xorw %%ax, %%ax  \n"
        "movw %%ax, %%fs  \n"
        "movw %%ax, %%gs  \n"
        /* Reload CS via far return */
        "pushq $0x08      \n"
        "leaq 1f(%%rip), %%rax \n"
        "pushq %%rax      \n"
        "lretq            \n"
        "1:               \n"
        ::: "rax", "memory");
}

/* =========================================================================
 * TSS
 * ========================================================================= */

tss_t tss __attribute__((aligned(16)));

static void tss_init(void)
{
    uint64_t base  = (uint64_t)&tss;
    uint32_t limit = sizeof(tss) - 1;

    __builtin_memset(&tss, 0, sizeof(tss));
    tss.iopb_offset = sizeof(tss);

    /* 16-byte TSS descriptor in GDT slots 5/6 */
    gdt_tss_entry_t *te = (gdt_tss_entry_t *)&gdt[5];
    te->limit_lo       = limit & 0xFFFF;
    te->base_lo        = base & 0xFFFF;
    te->base_mid       = (base >> 16) & 0xFF;
    te->access         = 0x89;   /* P=1, DPL=0, type=9 (available 64-bit TSS) */
    te->flags_limit_hi = (limit >> 16) & 0x0F;
    te->base_hi        = (base >> 24) & 0xFF;
    te->base_upper     = (uint32_t)(base >> 32);
    te->reserved       = 0;

    gdt_ptr_t gdtr = {
        .limit = sizeof(gdt) - 1,
        .base  = (uint64_t)&gdt,
    };
    lgdt_inline(&gdtr);
    __asm__ volatile ("ltr %%ax" :: "a"((uint16_t)GDT_TSS));
}

/* =========================================================================
 * IDT
 * ========================================================================= */

static idt_entry_t idt[IDT_ENTRIES] __attribute__((aligned(16)));

void idt_set_gate(uint8_t num, uint64_t handler, uint16_t sel,
                  uint8_t type_attr, uint8_t ist)
{
    idt[num].offset_lo  = handler & 0xFFFF;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_hi  = (uint32_t)(handler >> 32);
    idt[num].selector   = sel;
    idt[num].ist        = ist & 0x07;
    idt[num].type_attr  = type_attr;
    idt[num].reserved   = 0;
}

static void idt_init(void)
{
    __builtin_memset(idt, 0, sizeof(idt));

    idt_ptr_t idtr = {
        .limit = sizeof(idt) - 1,
        .base  = (uint64_t)&idt,
    };
    lidt_inline(&idtr);
}

/* Register the 32 CPU exceptions + the syscall gate. */
static void isr_init(void)
{
    uint8_t vec;
    for (vec = 0; vec < 32; vec++) {
        /* isr0..isr31 are defined as data in isr.s (see there). */
        extern void (*isr_stub_table[32])(void);
        idt_set_gate(vec, (uint64_t)isr_stub_table[vec],
                     GDT_KERNEL_CODE, IDT_GATE_INT32, 0);
    }
}

/* Set up the syscall/sysret MSRs (STAR/LSTAR/SFMASK).
 * STAR bits 47:32 = SYSCALL CS (kernel code 0x08; SS = +8 = 0x10).
 * STAR bits 63:48 = SYSRET base (CS = base+16 = 0x20, SS = base+8 = 0x18). */
static void syscall_init(void)
{
    /* Enable the syscall/sysret instructions (EFER.SCE). */
    uint64_t efer = rdmsr(0xC0000080);
    efer |= 1;   /* SCE */
    wrmsr(0xC0000080, efer);

    /* STAR bits 47:32 = SYSCALL CS (kernel code 0x08; SS = +8 = 0x10).
     * STAR bits 63:48 = SYSRET base (CS = base+16 = 0x20, SS = base+8 = 0x18). */
    uint64_t star = ((uint64_t)0x10 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(0xC0000081, star);                    /* STAR */
    wrmsr(0xC0000082, (uint64_t)syscall_entry); /* LSTAR */
    wrmsr(0xC0000084, 0x200);                   /* SFMASK: mask IF on entry */
}

/* =========================================================================
 * Arch-neutral API
 * ========================================================================= */

extern uint64_t boot_pml4[];      /* arch/x86_64/boot.s (low, identity) */
volatile uintptr_t kernel_page_table;   /* set by boot.s = phys of boot_pml4 */

uint64_t *arch_kernel_pgdir(void)
{
    /* boot_pml4 lives at a low physical address; return the direct-map
     * alias so dereferences work even under a user PML4. */
    return (uint64_t *)PHYS_TO_VIRT((uintptr_t)boot_pml4);
}

uintptr_t arch_kernel_pgdir_phys(void)
{
    return (uintptr_t)kernel_page_table;
}

void arch_set_kernel_stack(uintptr_t esp0)
{
    tss.rsp0 = esp0;
}

void arch_cpu_init(void)
{
    gdt_init();
    tss_init();
    idt_init();
    isr_init();
    irq_init();              /* install IDT gates for IRQs 0-15 */
    pic_init(0x20, 0x28);    /* early PIC (replaced by APIC later) */
    syscall_init();
}
