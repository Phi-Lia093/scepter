#include "driver/char/mouse.h"
#include "driver/char/char.h"
#include "arch/irq.h"
#include "fs/devfs.h"
#include "arch/io.h"
#include "lib/printk.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * PS/2 Mouse Driver
 *
 * Uses the standard 8042 controller protocol to enable the auxiliary
 * device, then streams 3-byte packets [flags, dx, dy] to a ring buffer
 * consumed through /dev/mouse (char device 15).
 * ========================================================================= */

#define PS2_DATA_PORT   0x60
#define PS2_CMD_PORT    0x64
#define IRQ_MOUSE       12

#define PS2_CMD_READ_CFG     0x20
#define PS2_CMD_WRITE_CFG    0x60
#define PS2_CMD_ENABLE_AUX   0xA8
#define PS2_CMD_SEND_TO_AUX  0xD4
#define AUX_CMD_SET_DEFAULTS 0xF6
#define AUX_CMD_ENABLE_REPORT 0xF4
#define AUX_ACK              0xFA

#define MOUSE_BUFFER_SIZE 256
#define PACKET_SIZE       3

/* =========================================================================
 * Mouse State
 * ========================================================================= */

static struct {
    uint8_t buffer[MOUSE_BUFFER_SIZE];   /* byte stream (packets) */
    int     read_pos;
    int     write_pos;
    int     count;
    uint8_t packet[PACKET_SIZE];         /* packet reassembly */
    int     packet_len;
} mouse_state = {0};

/* -------------------------------------------------------------------------
 * 8042 helpers
 * ------------------------------------------------------------------------- */

/* Wait until the controller's input buffer is empty (safe to write). */
static void ps2_wait_input_empty(void)
{
    int timeout = 100000;
    while (timeout-- > 0) {
        if (!(inb(PS2_CMD_PORT) & 0x02))
            return;
    }
}

/* Wait until the controller has data in its output buffer. */
static void ps2_wait_output(void)
{
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(PS2_CMD_PORT) & 0x01)
            return;
    }
}

/* Send one command byte to the auxiliary device and wait for its ACK. */
static void ps2_send_aux_cmd(uint8_t cmd)
{
    ps2_wait_input_empty();
    outb(PS2_CMD_PORT, PS2_CMD_SEND_TO_AUX);
    ps2_wait_input_empty();
    outb(PS2_DATA_PORT, cmd);

    ps2_wait_output();
    (void)inb(PS2_DATA_PORT);   /* consume the 0xFA ACK */
}

/* -------------------------------------------------------------------------
 * Ring buffer
 * ------------------------------------------------------------------------- */

static void mouse_buffer_push(uint8_t c)
{
    if (mouse_state.count < MOUSE_BUFFER_SIZE) {
        mouse_state.buffer[mouse_state.write_pos] = c;
        mouse_state.write_pos =
            (mouse_state.write_pos + 1) % MOUSE_BUFFER_SIZE;
        mouse_state.count++;
    }
}

static int mouse_buffer_pop(void)
{
    if (mouse_state.count > 0) {
        uint8_t c = mouse_state.buffer[mouse_state.read_pos];
        mouse_state.read_pos =
            (mouse_state.read_pos + 1) % MOUSE_BUFFER_SIZE;
        mouse_state.count--;
        return c;
    }
    return 0;
}
/* -------------------------------------------------------------------------
 * IRQ12 handler
 *
 * The IRQ stub passes the interrupted CS; we don't need it here.  Packets
 * are 3 bytes: [flags, dx, dy].  The first byte must have bit 3 set, which
 * we use to resynchronise after any dropped bytes.
 * ------------------------------------------------------------------------- */

void mouse_isr(uint32_t cs)
{
    (void)cs;

    uint8_t byte = inb(PS2_DATA_PORT);

    mouse_state.packet[mouse_state.packet_len++] = byte;

    if (mouse_state.packet_len == 1) {
        /* First byte: bit 3 is always set; anything else means we are
         * mid-packet (e.g. a dropped byte) — resynchronise. */
        if (!(byte & 0x08)) {
            mouse_state.packet_len = 0;
            return;
        }
        return;
    }

    if (mouse_state.packet_len == PACKET_SIZE) {
        for (int i = 0; i < PACKET_SIZE; i++)
            mouse_buffer_push(mouse_state.packet[i]);
        mouse_state.packet_len = 0;
        /* Wake blocked readers. */
        char_wakeup(CHAR_DEV_MOUSE);
    }
}

/* -------------------------------------------------------------------------
 * Driver callbacks
 * ------------------------------------------------------------------------- */

static int mouse_read(int scnd_id)
{
    if (scnd_id != 0)
        return 0;
    return mouse_buffer_pop();
}

static int mouse_poll(int scnd_id)
{
    (void)scnd_id;
    return mouse_state.count > 0;
}

static int mouse_write(int scnd_id, char c)
{
    (void)scnd_id;
    (void)c;
    return -1;
}

static int mouse_ioctl(int prim_id, int scnd_id, unsigned int command,
                       uint32_t arg)
{
    (void)prim_id;
    (void)scnd_id;
    (void)command;
    (void)arg;
    return -1;
}

/* -------------------------------------------------------------------------
 * Initialisation – 8042 aux enable + IRQ setup + registration
 * ------------------------------------------------------------------------- */

void mouse_init(void)
{
    mouse_state.read_pos   = 0;
    mouse_state.write_pos  = 0;
    mouse_state.count      = 0;
    mouse_state.packet_len = 0;

    /* Enable the auxiliary device. */
    ps2_wait_input_empty();
    outb(PS2_CMD_PORT, PS2_CMD_ENABLE_AUX);

    /* Read the 8042 configuration byte, enable IRQ12 (bit 1) and the aux
     * clock (clear bit 5), preserving the keyboard's IRQ1 bits. */
    ps2_wait_input_empty();
    outb(PS2_CMD_PORT, PS2_CMD_READ_CFG);
    ps2_wait_output();
    uint8_t cfg = inb(PS2_DATA_PORT);
    cfg |= 0x02;        /* enable aux device interrupt (IRQ12) */
    cfg &= ~0x20;       /* enable aux device clock */
    ps2_wait_input_empty();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_CFG);
    ps2_wait_input_empty();
    outb(PS2_DATA_PORT, cfg);

    /* Put the mouse in defaults, then enable data reporting. */
    ps2_send_aux_cmd(AUX_CMD_SET_DEFAULTS);
    ps2_send_aux_cmd(AUX_CMD_ENABLE_REPORT);

    /* Register IRQ12 handler through the generic IRQ dispatcher. */
    irq_register(IRQ_MOUSE, mouse_isr);

    char_ops_t ops = {
        .read  = mouse_read,
        .write = mouse_write,
        .poll  = mouse_poll,
        .ioctl = mouse_ioctl,
    };
    register_char_device(CHAR_DEV_MOUSE, &ops);
    char_set_blocking(CHAR_DEV_MOUSE, 1);
    devfs_register_device("mouse", DT_CHRDEV, CHAR_DEV_MOUSE, 0);

    printk("[MOUSE] PS/2 mouse initialized (/dev/mouse)\n");
}

