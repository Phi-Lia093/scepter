#include "arch/timer.h"
#include "arch/irq.h"
#include "arch/io.h"
#include "driver/char/char.h"
#include "kernel/sched.h"
#include "fs/devfs.h"

#define IRQ0  0  /* Timer IRQ */

/* 8253/8254 Programmable Interval Timer – channel 0 (IRQ0) */
#define PIT_CHANNEL0  0x40   /* channel 0 data port  */
#define PIT_CMD       0x43   /* mode/command register */

/* Command byte: channel 0, lobyte/hibyte access, mode 3 (square wave) */
#define PIT_CMD_INIT  0x36

/* PIT input clock frequency (Hz) */
#define PIT_BASE_HZ   1193182UL

/* =========================================================================
 * Driver state
 * ========================================================================= */

static volatile uint32_t timer_ticks = 0;

/* Global wait queue for timer sleeps (nanosleep). timer_isr wakes it on
 * every tick; sleeping tasks re-check their deadline and re-sleep. */
wait_queue_head_t timer_wq;

uint32_t arch_timer_get_ticks(void)
{
    return timer_ticks;
}

/* Latch and read the free-running timer counter (implementation-defined
 * units).  Used by drivers for short busy-waits while interrupts are off. */
uint16_t arch_timer_read_count(void)
{
    uint16_t v;
    outb(PIT_CMD, 0x00);                  /* latch channel 0 */
    v  = (uint16_t)inb(PIT_CHANNEL0);
    v |= (uint16_t)inb(PIT_CHANNEL0) << 8;
    return v;
}

/* =========================================================================
 * IRQ0 handler – called from the irq0 stub in isr.s
 * The stub passes the interrupted CS as the single argument, so we can
 * tell user mode (0x1B) from kernel mode (0x08).
 * ========================================================================= */

#define USER_CS_SEL 0x1B

void timer_isr(uint32_t cs)
{
    timer_ticks++;
    /* EOI is handled by irq_dispatch() after the handler returns. */

    /* Mix timer-phase jitter into the entropy pool. */
    extern void random_add_entropy(uint32_t bits);
    random_add_entropy(timer_ticks);

    /* CPU-time accounting: charge this tick to the current task.  A tick
     * that hit user code is user time, a tick inside a syscall/IRQ is
     * system time (ITIMER_VIRTUAL/PROF + times()/getrusage depend on it). */
    int in_user = (cs == USER_CS_SEL);
    if (current && current->pid != 0) {
        if (in_user)
            current->uticks++;
        else
            current->sticks++;
    }

    /* Interval timers + CPU-time accounting (SIGALRM/SIGVTALRM/SIGPROF) */
    extern void timer_tick(int in_user);
    timer_tick(in_user);

    /* Auto-silence the PC speaker after a short beep (write() path). */
    extern void pcspk_tick(void);
    pcspk_tick();

    /* Wake nanosleep() waiters every tick (100 Hz) */
    wake_up(&timer_wq);

    /* Wake select()/poll() waiters every tick so their timeouts fire. */
    extern void vfs_poll_wakeup(void);
    vfs_poll_wakeup();

    /* Call scheduler every 10 ticks (100ms at 100Hz) */
    if (timer_ticks % 10 == 0) {
        schedule();
    }
}
/* =========================================================================
 * Driver callbacks
 * ========================================================================= */

static int pit_read(int scnd_id)
{
    (void)scnd_id;
    return (char)(timer_ticks & 0xFF);
}

static int pit_write(int scnd_id, char c)
{
    (void)scnd_id;
    (void)c;
    return 0;
}

/* =========================================================================
 * Initialisation – hardware + IRQ setup + driver registration + devfs node
 * ========================================================================= */

void arch_timer_init(uint32_t hz)
{
    /* Calculate divisor, clamp to valid range [1, 65535] */
    uint32_t divisor = PIT_BASE_HZ / hz;
    if (divisor == 0)    divisor = 1;
    if (divisor > 65535) divisor = 65535;

    /* Program PIT channel 0: mode 3 (square wave), binary counting */
    outb(PIT_CMD,      PIT_CMD_INIT);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));

    /* Register IRQ0 handler through the generic IRQ dispatcher.
     * irq_init() already installed the IDT gate (vector 32); this just
     * records the handler and unmasks the IRQ on the active controller. */
    irq_register(IRQ0, timer_isr);

    /* Initialize the timer wake-up queue used by nanosleep() */
    init_waitqueue_head(&timer_wq);

    /* Register as char device 1 and add devfs node */
    char_ops_t ops = { .read = pit_read, .write = pit_write, .ioctl = NULL };
    register_char_device(1, &ops);
    devfs_register_device("pit0", DT_CHRDEV, 1, 0);
}