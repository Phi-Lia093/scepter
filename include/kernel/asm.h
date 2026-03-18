#ifndef ASM_H
#define ASM_H

#include <stdint.h>

/* =========================================================================
 * Reusable x86 inline assembly primitives
 * ========================================================================= */

/* Write a byte to an I/O port */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a byte from an I/O port */
static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Write a 16-bit word to an I/O port */
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a 16-bit word from an I/O port */
static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Write a 32-bit dword to an I/O port */
static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a 32-bit dword from an I/O port */
static inline uint32_t inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Short I/O delay (write to unused port 0x80) */
static inline void io_wait(void)
{
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

/* Disable / enable hardware interrupts */
static inline void cli(void)
{
    __asm__ volatile ("cli");
}

static inline void sti(void)
{
    __asm__ volatile ("sti");
}

/* Halt the CPU until the next interrupt */
static inline void hlt(void)
{
    __asm__ volatile ("hlt");
}

static inline void magic_break(void)
{
    __asm__ volatile ("xchgw %bx, %bx");
}

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

#endif /* ASM_H */
