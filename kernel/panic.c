#include "kernel/panic.h"
#include "lib/printk.h"
#include "kernel/asm.h"
#include <stdint.h>

/* =========================================================================
 * Exception names (Intel SDM Vol.3 Table 6-1)
 * ========================================================================= */

static const char *exception_names[32] = {
    "#DE Divide Error",
    "#DB Debug",
    "NMI Interrupt",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR BOUND Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection",
    "#PF Page Fault",
    "Reserved (15)",
    "#MF x87 FPU Error",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD Floating-Point",
    "#VE Virtualization",
    "#CP Control Protection",
    "Reserved (22)",
    "Reserved (23)",
    "Reserved (24)",
    "Reserved (25)",
    "Reserved (26)",
    "Reserved (27)",
    "#HV Hypervisor Injection",
    "#VC VMM Communication",
    "#SX Security Exception",
    "Reserved (31)",
};

/* =========================================================================
 * Read control registers via inline asm
 * ========================================================================= */

static inline uint32_t read_cr0(void)
{
    uint32_t v;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline uint32_t read_cr2(void)
{
    uint32_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline uint32_t read_cr3(void)
{
    uint32_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline uint32_t read_cr4(void)
{
    uint32_t v;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(v));
    return v;
}

/* =========================================================================
 * panic_isr – called from isr.s with the saved register frame
 * ========================================================================= */

void panic_isr(regs_t *r)
{
    const char *name = (r->int_no < 32)
                       ? exception_names[r->int_no]
                       : "Unknown Exception";

    printk("\n\n*** CPU EXCEPTION ***\n");
    printk("Vector : %u  %s\n", r->int_no, name);
    printk("ErrCode: 0x%08x\n\n", r->err_code);

    /* General-purpose registers */
    printk("EAX=%08x  EBX=%08x  ECX=%08x  EDX=%08x\n",
           r->eax, r->ebx, r->ecx, r->edx);
    printk("ESI=%08x  EDI=%08x  EBP=%08x\n",
           r->esi, r->edi, r->ebp);

    /* Instruction / stack pointers */
    printk("EIP=%08x  EFLAGS=%08x\n", r->eip, r->eflags);

    /* Segment registers
     * SS and user ESP are only pushed by the CPU on a ring-3 -> ring-0 transition.
     * If CS DPL == 0 (kernel exception), those stack slots don't exist. */
    int from_userspace = (r->cs & 0x3) == 3;
    if (from_userspace) {
        printk("CS=%04x  DS=%04x  ES=%04x  FS=%04x  GS=%04x  SS=%04x\n",
               r->cs, r->ds, r->es, r->fs, r->gs, r->ss);
        printk("ESP(user)=%08x\n", r->useresp);
    } else {
        printk("CS=%04x  DS=%04x  ES=%04x  FS=%04x  GS=%04x  SS=<kernel>\n",
               r->cs, r->ds, r->es, r->fs, r->gs);
    }

    /* Control registers */
    printk("CR0=%08x  CR2=%08x  CR3=%08x  CR4=%08x\n",
           read_cr0(), read_cr2(), read_cr3(), read_cr4());

    /* Stack dump – print 8 words from the faulting stack */
    printk("\nStack dump (EIP area):\n");
    uint32_t *stack = (uint32_t *)r->eip;
    for (int i = 0; i < 8; i++)
        printk("  [%08x] %08x\n",
               (uint32_t)(stack + i), stack[i]);

    panic(name);
}

/* =========================================================================
 * panic – print message, disable interrupts, halt forever
 * ========================================================================= */

void panic(const char *msg)
{
    printk("\nKERNEL PANIC: %s\n", msg);
    cli();
    for (;;)
        hlt();
}
