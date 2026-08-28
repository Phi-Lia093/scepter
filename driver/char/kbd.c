#include "driver/char/kbd.h"
#include "driver/char/char.h"
#include "driver/char/tty.h"
#include "driver/apic/interrupt.h"
#include "kernel/cpu.h"
#include "kernel/signal.h"
#include "fs/devfs.h"
#include "kernel/asm.h"

/* =========================================================================
 * PS/2 Keyboard Constants
 * ========================================================================= */

#define KBD_DATA_PORT    0x60
#define KBD_BUFFER_SIZE  128

#define IRQ1             1    /* Keyboard IRQ */

#define SC_LSHIFT        0x2A
#define SC_RSHIFT        0x36
#define SC_LSHIFT_REL    0xAA
#define SC_RSHIFT_REL    0xB6
#define SC_CAPSLOCK      0x3A
#define SC_LCTRL         0x1D
#define SC_LCTRL_REL     0x9D
#define SC_RCTRL         0x1D
#define SC_RCTRL_REL     0x9D

/* =========================================================================
 * Keyboard State
 * ========================================================================= */

static struct {
    char    buffer[KBD_BUFFER_SIZE];
    int     read_pos;
    int     write_pos;
    int     count;
    uint8_t shift_pressed;
    uint8_t caps_lock;
    uint8_t ctrl_pressed;
} kbd_state = {0};

/* =========================================================================
 * Scancode to ASCII Translation Tables
 * ========================================================================= */

static const char scancode_to_ascii[] = {
    0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
    'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  0,    0,    0,    0
};

static const char scancode_to_ascii_shift[] = {
    0,    0x1B, '!',  '@',  '#',  '$',  '%',  '^',
    '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    '7',
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  0,    0,    0,    0
};

/* =========================================================================
 * Circular Buffer
 * ========================================================================= */

static void kbd_buffer_push(char c)
{
    if (kbd_state.count < KBD_BUFFER_SIZE) {
        kbd_state.buffer[kbd_state.write_pos] = c;
        kbd_state.write_pos = (kbd_state.write_pos + 1) % KBD_BUFFER_SIZE;
        kbd_state.count++;
    }
}

static char kbd_buffer_pop(void)
{
    if (kbd_state.count > 0) {
        char c = kbd_state.buffer[kbd_state.read_pos];
        kbd_state.read_pos = (kbd_state.read_pos + 1) % KBD_BUFFER_SIZE;
        kbd_state.count--;
        return c;
    }
    return 0;
}

/* =========================================================================
 * IRQ1 Handler
 * The IRQ stub passes the interrupted CS; we don't need it here.
 * ========================================================================= */

void kbd_isr(uint32_t cs)
{
    (void)cs;
    uint8_t scancode = inb(KBD_DATA_PORT);

    /* Keyboard timing/scan code is a decent entropy source. */
    extern void random_add_entropy(uint32_t bits);
    extern uint32_t pit_get_ticks(void);
    random_add_entropy((uint32_t)scancode ^ pit_get_ticks());

    if (scancode == SC_LSHIFT || scancode == SC_RSHIFT) {
        kbd_state.shift_pressed = 1;
        return;
    }
    if (scancode == SC_LSHIFT_REL || scancode == SC_RSHIFT_REL) {
        kbd_state.shift_pressed = 0;
        return;
    }
    if (scancode == SC_CAPSLOCK) {
        kbd_state.caps_lock = !kbd_state.caps_lock;
        return;
    }
    if (scancode == SC_LCTRL) {
        kbd_state.ctrl_pressed = 1;
        return;
    }
    if (scancode == SC_LCTRL_REL) {
        kbd_state.ctrl_pressed = 0;
        return;
    }
    if (scancode & 0x80) {   /* break code – ignore */
        return;
    }

    char ascii = 0;
    if (scancode < sizeof(scancode_to_ascii)) {
        ascii = kbd_state.shift_pressed
                ? scancode_to_ascii_shift[scancode]
                : scancode_to_ascii[scancode];

        if (kbd_state.caps_lock && ascii >= 'a' && ascii <= 'z')
            ascii = ascii - 'a' + 'A';
        else if (kbd_state.caps_lock && ascii >= 'A' && ascii <= 'Z')
            ascii = ascii - 'A' + 'a';

        /* Ctrl + letter => control character (Ctrl-C = 0x03, ...) */
        if (kbd_state.ctrl_pressed && ascii >= 'a' && ascii <= 'z')
            ascii = ascii - 'a' + 1;
        else if (kbd_state.ctrl_pressed && ascii >= 'A' && ascii <= 'Z')
            ascii = ascii - 'A' + 1;
    }

    if (ascii != 0) {
        /* Control characters (^C, ^\, ^Z) may be consumed by the tty line
         * discipline as terminal signals (ISIG).  If they are not consumed
         * they go into the input buffer like any other byte. */
        extern int tty_handle_control(int ascii);
        if (!tty_handle_control(ascii))
            kbd_buffer_push(ascii);
        /* Wake any process blocked in read() so it can notice the byte
         * (or the pending signal -> char_read_block returns -EINTR). */
        char_wakeup(3);
    }
}

/* =========================================================================
 * Driver callbacks
 * ========================================================================= */

static int kbd_read(int scnd_id)
{
    if (scnd_id != 0) return 0;
    return kbd_buffer_pop();
}

static int kbd_poll(int scnd_id)
{
    (void)scnd_id;
    return kbd_state.count > 0;
}

static int kbd_write(int scnd_id, char c)
{
    (void)scnd_id; (void)c;
    return -1;
}

static int kbd_ioctl(int prim_id, int scnd_id, unsigned int command, uint32_t arg)
{
    (void)prim_id; (void)scnd_id; (void)command; (void)arg;
    return -1;
}

/* =========================================================================
 * Initialisation – IRQ setup + driver registration + devfs node
 * ========================================================================= */

void kbd_init(void)
{
    kbd_state.read_pos      = 0;
    kbd_state.write_pos     = 0;
    kbd_state.count         = 0;
    kbd_state.shift_pressed = 0;
    kbd_state.caps_lock     = 0;

    /* Register IRQ1 handler through the generic IRQ dispatcher. */
    irq_register(IRQ1, kbd_isr);

    char_ops_t ops = { .read = kbd_read, .write = kbd_write, .poll = kbd_poll, .ioctl = kbd_ioctl };
    register_char_device(3, &ops);
    char_set_blocking(3, 1);   /* reads block until a key is pressed */
    devfs_register_device("kbd0", DT_CHRDEV, 3, 0);
}