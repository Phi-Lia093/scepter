/* ============================================================================
 * Exception entry (x86_64): panic_isr is called from arch/x86_64/isr.s
 * ============================================================================ */

#include "arch/cpu.h"
#include "kernel/sched.h"
#include "kernel/panic.h"
#include "mm/pagefault.h"
#include "lib/printk.h"
#include <stdint.h>

/* register frame pushed by isr.s (see arch/x86_64/isr.s) */
typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t int_no;
    uint64_t err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} regs_t;

static const char *exception_names[32] = {
    "#DE Divide Error",  "#DB Debug",  "NMI Interrupt",  "#BP Breakpoint",
    "#OF Overflow",  "#BR BOUND Range Exceeded",  "#UD Invalid Opcode",
    "#NM Device Not Available",  "#DF Double Fault",
    "Coprocessor Segment Overrun",  "#TS Invalid TSS",
    "#NP Segment Not Present",  "#SS Stack-Segment Fault",
    "#GP General Protection",  "#PF Page Fault",  "Reserved (15)",
    "#MF x87 FPU Error",  "#AC Alignment Check",  "#MC Machine Check",
    "#XM SIMD Floating-Point",  "#VE Virtualization",  "#CP Control Protection",
    "Reserved (22)",  "Reserved (23)",  "Reserved (24)",  "Reserved (25)",
    "Reserved (26)",  "Reserved (27)",  "#HV Hypervisor Injection",
    "#VC VMM Communication",  "#SX Security Exception",  "Reserved (31)",
};

static inline uint64_t read_cr2(void)
{
    uint64_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

void panic_isr(regs_t *r)
{
    /* Page fault (vector 14) */
    if (r->int_no == 14) {
        uint64_t fault_addr = read_cr2();
        if (!(r->err_code & 0x4)) {   /* kernel fault: verbose dump */
            extern task_struct_t *current;
            printk("\n[PAGEFAULT] INT14 cr2=0x%lx err=0x%lx RIP=0x%lx CS=0x%lx pid=%u\n",
                   (unsigned long)fault_addr, (unsigned long)r->err_code,
                   (unsigned long)r->rip, (unsigned long)r->cs,
                   current ? current->pid : 0xffffffffu);
            printk("[PAGEFAULT] RAX=%lx RBX=%lx RCX=%lx RDX=%lx\n",
                   (unsigned long)r->rax, (unsigned long)r->rbx,
                   (unsigned long)r->rcx, (unsigned long)r->rdx);
        }
        page_fault_handler((uint32_t)r->err_code, (uintptr_t)fault_addr);
        return;
    }

    const char *name = (r->int_no < 32)
                       ? exception_names[r->int_no]
                       : "Unknown Exception";

    printk("\n\n*** CPU EXCEPTION ***\n");
    printk("Vector : %lu  %s\n", (unsigned long)r->int_no, name);
    printk("ErrCode: 0x%lx\n\n", (unsigned long)r->err_code);

    printk("RAX=%016lx RBX=%016lx RCX=%016lx RDX=%016lx\n",
           (unsigned long)r->rax, (unsigned long)r->rbx,
           (unsigned long)r->rcx, (unsigned long)r->rdx);
    printk("RSI=%016lx RDI=%016lx RBP=%016lx RSP=%016lx\n",
           (unsigned long)r->rsi, (unsigned long)r->rdi,
           (unsigned long)r->rbp, (unsigned long)r->rsp);
    printk("R8 =%016lx R9 =%016lx R10=%016lx R11=%016lx\n",
           (unsigned long)r->r8, (unsigned long)r->r9,
           (unsigned long)r->r10, (unsigned long)r->r11);
    printk("R12=%016lx R13=%016lx R14=%016lx R15=%016lx\n",
           (unsigned long)r->r12, (unsigned long)r->r13,
           (unsigned long)r->r14, (unsigned long)r->r15);
    printk("RIP=%016lx  RFLAGS=%016lx  CS=%04lx  SS=%04lx\n",
           (unsigned long)r->rip, (unsigned long)r->rflags,
           (unsigned long)r->cs, (unsigned long)r->ss);

    panic(name);
}
