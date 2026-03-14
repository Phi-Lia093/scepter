#include "driver/char/kbd.h"
#include "driver/char/char.h"
#include "driver/pic.h"
#include "kernel/cpu.h"
#include "fs/devfs.h"
#include "kernel/asm.h"

/* =========================================================================
 * PS/2 Keyboard Constants
 * ========================================================================= */

#define KBD_DATA_PORT    0x60
#define KBD_BUFFER_SIZE  128

#define SC_LSHIFT        0x2A
#define SC_RSHIFT        0x36
#define SC_LSHIFT_REL    0xAA
#define SC_RSHIFT_REL    0xB6
#define SC_CAPSLOCK      0x3A

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
 * ========================================================================= */

void kbd_isr(void)
{
    uint8_t scancode = inb(KBD_DATA_PORT);

    if (scancode == SC_LSHIFT || scancode == SC_RSHIFT) {
        kbd_state.shift_pressed = 1;
        pic_send_eoi(IRQ1);
        return;
    }
    if (scancode == SC_LSHIFT_REL || scancode == SC_RSHIFT_REL) {
        kbd_state.shift_pressed = 0;
        pic_send_eoi(IRQ1);
        return;
    }
    if (scancode == SC_CAPSLOCK) {
        kbd_state.caps_lock = !kbd_state.caps_lock;
        pic_send_eoi(IRQ1);
        return;
    }
    if (scancode & 0x80) {   /* break code – ignore */
        pic_send_eoi(IRQ1);
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
    }

    if (ascii != 0) kbd_buffer_push(ascii);
    pic_send_eoi(IRQ1);
}

/* =========================================================================
 * Driver callbacks
 * ========================================================================= */

static char kbd_read(int scnd_id)
{
    if (scnd_id != 0) return 0;
    return kbd_buffer_pop();
}

static int kbd_write(int scnd_id, char c)
{
    (void)scnd_id; (void)c;
    return -1;
}

static int kbd_ioctl(int prim_id, int scnd_id, unsigned int command)
{
    (void)prim_id; (void)scnd_id; (void)command;
    return -1;
}

/* =========================================================================
 * Initialisation – IRQ setup + driver registration + devfs node
 * ========================================================================= */

extern void irq1(void);   /* defined in kernel/isr.s */

void kbd_init(void)
{
    kbd_state.read_pos      = 0;
    kbd_state.write_pos     = 0;
    kbd_state.count         = 0;
    kbd_state.shift_pressed = 0;
    kbd_state.caps_lock     = 0;

    idt_set_gate(33, (uint32_t)irq1, GDT_KERNEL_CODE, IDT_GATE_INT32);
    pic_enable_irq(IRQ1);

    char_ops_t ops = { .read = kbd_read, .write = kbd_write, .ioctl = kbd_ioctl };
    register_char_device(3, &ops);
    devfs_register_device("kbd0", DT_CHRDEV, 3, 0);
}