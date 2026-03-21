#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

/* =========================================================================
 * CPU register frame pushed by isr.s
 *
 * Stack layout at the point panic_isr is called (low → high address):
 *   gs, fs, es, ds          (pushed by isr_common, reversed order)
 *   edi..eax                (pusha)
 *   int_no, err_code        (pushed by stub)
 *   eip, cs, eflags         (pushed by CPU)
 *   useresp, ss             (pushed by CPU on privilege change)
 * ========================================================================= */

typedef struct {
    uint32_t cr3_saved;               /* saved CR3 (pushed before call) */
    uint32_t gs, fs, es, ds;          /* segment registers (pushed in this order) */
    uint32_t edi, esi, ebp,
             esp_dummy,               /* ESP value at pusha time (not usable)   */
             ebx, edx, ecx, eax;     /* general-purpose registers               */
    uint32_t int_no;                  /* exception vector number                 */
    uint32_t err_code;                /* error code (0 if none)                  */
    /* pushed by CPU automatically: */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t useresp;                 /* only valid on ring-3 → ring-0 switch    */
    uint32_t ss;
} regs_t;

/* Called from isr.s with a pointer to the saved register frame */
void panic_isr(regs_t *r);

/* General kernel panic – prints message, disables interrupts, halts */
void panic(const char *msg);

#endif /* PANIC_H */
