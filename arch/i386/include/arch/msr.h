#ifndef ARCH_I386_MSR_H
#define ARCH_I386_MSR_H

#include <stdint.h>

/* =========================================================================
 * PRIVATE i386 header – Model-Specific Register access.
 * ========================================================================= */

/* Read Model Specific Register (MSR) */
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

/* Write Model Specific Register (MSR) */
static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile ("wrmsr" :: "c"(msr), "a"(low), "d"(high));
}

/* MSR Numbers */
#define IA32_APIC_BASE  0x1B

#endif /* ARCH_I386_MSR_H */
