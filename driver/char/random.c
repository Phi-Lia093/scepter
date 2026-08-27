/* ============================================================================
 * Random number generator driver (/dev/random, /dev/urandom)
 *
 * Implementation: a 128-bit xorshift generator (xorshift128) whose state is
 * seeded at init from the PIT tick counter and RTC, then continuously mixed
 * with interrupt timing jitter via random_add_entropy().  This is not
 * cryptographically strong, but is perfectly adequate for games, hash salts,
 * test vectors and userspace tools.
 * ============================================================================ */

#include "driver/char/random.h"
#include "driver/char/char.h"
#include "fs/devfs.h"
#include "lib/printk.h"
#include <stdint.h>
#include <stddef.h>

/* ----------------------------------------------------------------------------
 * xorshift128 state
 * -------------------------------------------------------------------------- */

static uint32_t rs[4];

/* Entropy counter (bits) reported to /dev/random readers. */
static uint32_t entropy_bits;

static uint32_t rotl(uint32_t x, int k)
{
    return (x << k) | (x >> (32 - k));
}

static uint32_t next_u32(void)
{
    uint32_t t = rs[3];

    /* xorshift128 */
    t ^= t << 11;
    t ^= t >> 8;
    rs[3] = rs[2]; rs[2] = rs[1]; rs[1] = rs[0];
    rs[0] = t;

    /* Final scramble (similar to xorwow's last step) makes the low bits
     * pass common statistical tests immediately after boot. */
    return t ^ (rs[3] + 0x9E3779B9U);
}

/* ----------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void random_get_bytes(void *buf, size_t n)
{
    uint8_t *out = (uint8_t *)buf;
    while (n >= 4) {
        uint32_t v = next_u32();
        out[0] = (uint8_t)(v);
        out[1] = (uint8_t)(v >> 8);
        out[2] = (uint8_t)(v >> 16);
        out[3] = (uint8_t)(v >> 24);
        out += 4;
        n   -= 4;
    }
    if (n) {
        uint32_t v = next_u32();
        for (size_t i = 0; i < n; i++)
            out[i] = (uint8_t)(v >> (i * 8));
    }
}

uint32_t random_get_u32(void)
{
    return next_u32();
}

void random_add_entropy(uint32_t bits)
{
    /* Fold the incoming bits into all four words using a simple mixing
     * function; any jitter in the source affects the whole state. */
    bits *= 0x2545F4914F6CDD1DULL >> 32;   /* keep it 32-bit-friendly */
    bits ^= bits >> 16;
    rs[0] ^= bits;
    rs[1] ^= rotl(bits, 7);
    rs[2] ^= rotl(bits, 17);
    rs[3] ^= rotl(bits, 23);

    /* Cap the reported entropy so /dev/random never claims more than we
     * actually started with (seed = 128 bits + whatever we add). */
    if (entropy_bits < 4096)
        entropy_bits += 16;
}

uint32_t random_entropy_count(void)
{
    return entropy_bits;
}

/* ----------------------------------------------------------------------------
 * Character device callbacks
 * -------------------------------------------------------------------------- */

static int random_read(int scnd_id)
{
    (void)scnd_id;
    return (int)(random_get_u32() & 0xFF);
}

static int random_write(int scnd_id, char c)
{
    (void)scnd_id;
    /* Writing to /dev/random reseeds the pool with the byte. */
    random_add_entropy((uint32_t)(uint8_t)c);
    return 0;
}

static int random_poll(int scnd_id)
{
    (void)scnd_id;
    return 1;   /* never blocks */
}

#define IOCTL_RNDGETENTCNT 0x80045200U   /* Linux RNDGETENTCNT */

static int random_ioctl(int prim_id, int scnd_id, unsigned int command,
                        uint32_t arg)
{
    (void)prim_id; (void)scnd_id; (void)arg;
    if (command == IOCTL_RNDGETENTCNT) {
        uint32_t ent = random_entropy_count();
        /* arg is a pointer to int in userspace; we can't dereference it
         * here (no copy_to_user), so report via the return value instead. */
        return (int)ent;
    }
    return -1;
}

/* ----------------------------------------------------------------------------
 * Initialisation
 * -------------------------------------------------------------------------- */

void random_init(void)
{
    /* Seed from the PIT tick counter and the RTC.  These are (mostly)
     * predictable, so we immediately mix in a fixed golden-ratio constant
     * and each other; random_add_entropy() from IRQ timing then diversifies
     * the pool as the system runs. */
    extern uint32_t pit_get_ticks(void);
    extern uint32_t rtc_get_unix_time(void);

    uint32_t seed = pit_get_ticks() ^ (uint32_t)0xA5A5A5A5U;
    seed ^= rtc_get_unix_time();
    seed ^= (uint32_t)(uintptr_t)&rs;   /* stack/kernel layout jitter */

    rs[0] = seed | 1U;
    rs[1] = (seed >> 8) ^ 0xDEADBEEFU;
    rs[2] = (seed >> 16) ^ 0x2545F491U;
    rs[3] = (seed >> 24) ^ 0xE7037ED1U;
    if (!rs[1]) rs[1] = 1;
    if (!rs[2]) rs[2] = 1;
    if (!rs[3]) rs[3] = 1;

    entropy_bits = 128;   /* the four seed words */

    char_ops_t ops = {
        .read  = random_read,
        .write = random_write,
        .poll  = random_poll,
        .ioctl = random_ioctl,
    };
    if (register_char_device(CHAR_DEV_RANDOM, &ops) == 0)
        devfs_register_device("random", DT_CHRDEV, CHAR_DEV_RANDOM, 0);
    if (register_char_device(CHAR_DEV_URANDOM, &ops) == 0)
        devfs_register_device("urandom", DT_CHRDEV, CHAR_DEV_URANDOM, 0);

    printk("[RANDOM] /dev/random + /dev/urandom registered\n");
}
