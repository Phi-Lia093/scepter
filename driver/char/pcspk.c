/* ============================================================================
 * PC Speaker driver
 *
 * The PC speaker is driven by PIT channel 2 (port 0x42) plus the speaker
 * gate bits on port 0x61.  Writing a byte to a device is treated as a short
 * beep; the ioctl interface gives full frequency control.
 * ============================================================================ */

#include "driver/char/pcspk.h"
#include "driver/char/char.h"
#include "fs/devfs.h"
#include "kernel/asm.h"
#include "lib/printk.h"
#include <stdint.h>

#define PIT_CMD_PORT   0x43
#define PIT_CH2_PORT   0x42
#define PIT_BASE_HZ    1193182U

#define PIT_CMD_CH2    0xB6   /* channel 2, lobyte/hibyte, mode 3, binary */

#define SPEAKER_PORT   0x61
#define SPEAKER_GATE   0x01   /* bit 0: timer 2 gate  */
#define SPEAKER_DATA   0x02   /* bit 1: speaker data  */

static uint32_t current_freq = 0;

/* Auto-silence deadline (in PIT ticks).  set by write(); pcspk_tick()
 * (called from the PIT ISR) turns the speaker off once reached. */
static uint32_t beep_until = 0;

/** Called from the PIT ISR every tick so short beeps self-silence. */
void pcspk_tick(void)
{
    if (beep_until != 0) {
        extern uint32_t pit_get_ticks(void);
        if (pit_get_ticks() >= beep_until) {
            pcspk_off();
            beep_until = 0;
        }
    }
}

void pcspk_beep(uint32_t freq)
{
    if (freq == 0) {
        pcspk_off();
        return;
    }
    if (freq > 20000) freq = 20000;

    uint32_t div = PIT_BASE_HZ / freq;
    if (div < 2) div = 2;

    /* Program PIT channel 2: square wave, binary counting. */
    outb(PIT_CMD_PORT, PIT_CMD_CH2);
    outb(PIT_CH2_PORT, (uint8_t)(div & 0xFF));
    outb(PIT_CH2_PORT, (uint8_t)(div >> 8));

    /* Enable the speaker: gate + data bits. */
    uint8_t port61 = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, (uint8_t)(port61 | SPEAKER_GATE | SPEAKER_DATA));

    current_freq = freq;
}

void pcspk_off(void)
{
    uint8_t port61 = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, (uint8_t)(port61 & ~(SPEAKER_GATE | SPEAKER_DATA)));
    current_freq = 0;
}

/* ----------------------------------------------------------------------------
 * Character device callbacks
 * -------------------------------------------------------------------------- */

static int pcspk_read(int scnd_id)
{
    (void)scnd_id;
    return (int)(current_freq & 0xFF);
}

static int pcspk_write(int scnd_id, char c)
{
    (void)scnd_id;
    (void)c;
    /* Writing any byte produces a short standard beep (like ^G). */
    extern uint32_t pit_get_ticks(void);
    pcspk_beep(880);
    beep_until = pit_get_ticks() + 2;   /* ~20 ms */
    return 0;
}

static int pcspk_ioctl(int prim_id, int scnd_id, unsigned int command,
                       uint32_t arg)
{
    (void)prim_id; (void)scnd_id;
    switch (command) {
        case IOCTL_PCSPK_BEEP:
            if (arg == 0)
                pcspk_off();
            else
                pcspk_beep(arg);
            return 0;
        case IOCTL_PCSPK_GET:
            return (int)current_freq;
        default:
            return -1;
    }
}

void pcspk_init(void)
{
    pcspk_off();   /* ensure silence at boot */

    char_ops_t ops = {
        .read  = pcspk_read,
        .write = pcspk_write,
        .ioctl = pcspk_ioctl,
    };
    if (register_char_device(CHAR_DEV_PCSPK, &ops) == 0)
        devfs_register_device("pcspk", DT_CHRDEV, CHAR_DEV_PCSPK, 0);

    printk("[PCSPK] PC speaker driver initialized\n");
}
