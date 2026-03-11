#include "driver/block/cache.h"
#include "driver/driver.h"
#include "mm/slab.h"
#include "lib/list.h"
#include "lib/printk.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * LRU Cache Entry Structure
 * ========================================================================= */

typedef struct cache_entry {
    int      prim_id;           /* device primary ID   */
    int      scnd_id;           /* device secondary ID */
    uint32_t offset;            /* block number        */
    uint8_t *data;              /* block data          */
    int      dirty;             /* 1 = needs write-back */
    list_head_t node;           /* embedded in lru sentinel list */
} cache_entry_t;

/* =========================================================================
 * Global Cache State
 *
 * lru_list is a sentinel; entries are ordered most→least recently used
 * (head->next is the MRU entry, head->prev is the LRU entry).
 * ========================================================================= */

static LIST_HEAD(lru_list);
static uint32_t num_entries = 0;
static uint32_t stat_hits   = 0;
static uint32_t stat_misses = 0;

/* =========================================================================
 * Internal Helper Functions
 * ========================================================================= */

static void cache_memcpy(void *dest, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
}

static cache_entry_t *find_entry(int prim_id, int scnd_id, uint32_t offset)
{
    cache_entry_t *e;
    list_for_each_entry(e, &lru_list, node) {
        if (e->prim_id == prim_id &&
            e->scnd_id == scnd_id &&
            e->offset  == offset)
            return e;
    }
    return NULL;
}

static int writeback_entry(cache_entry_t *entry)
{
    if (!entry->dirty)
        return 0;

    int ret = bwrite(entry->prim_id, entry->scnd_id,
                     entry->data, entry->offset, 1);
    if (ret > 0) {
        entry->dirty = 0;
        return 0;
    }
    return -1;
}

/* Evict the LRU (tail) entry */
static int evict_lru(void)
{
    if (list_empty(&lru_list))
        return -1;

    cache_entry_t *victim = list_last_entry(&lru_list, cache_entry_t, node);

    if (victim->dirty) {
        if (writeback_entry(victim) < 0)
            printk("[CACHE] Warning: Failed to write back dirty block\n");
    }

    list_del(&victim->node);
    kfree(victim->data);
    kfree(victim);
    num_entries--;
    return 0;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void cache_init(void)
{
    INIT_LIST_HEAD(&lru_list);
    num_entries = 0;
    stat_hits   = 0;
    stat_misses = 0;

    printk("[CACHE] Initialized LRU cache: %d entries x %d bytes = %d KB\n",
           CACHE_MAX_ENTRIES, CACHE_BLOCK_SIZE,
           (CACHE_MAX_ENTRIES * CACHE_BLOCK_SIZE) / 1024);
}

int cache_lookup(int prim_id, int scnd_id, uint32_t offset, void *buf)
{
    cache_entry_t *entry = find_entry(prim_id, scnd_id, offset);

    if (entry) {
        stat_hits++;
        cache_memcpy(buf, entry->data, CACHE_BLOCK_SIZE);
        /* Promote to MRU position */
        list_move(&entry->node, &lru_list);
        return 1;
    }

    stat_misses++;
    return 0;
}

int cache_insert(int prim_id, int scnd_id, uint32_t offset, const void *data)
{
    cache_entry_t *existing = find_entry(prim_id, scnd_id, offset);
    if (existing) {
        cache_memcpy(existing->data, data, CACHE_BLOCK_SIZE);
        list_move(&existing->node, &lru_list);
        return 0;
    }

    if (num_entries >= CACHE_MAX_ENTRIES) {
        if (evict_lru() < 0)
            return -1;
    }

    cache_entry_t *entry = (cache_entry_t *)kalloc(sizeof(cache_entry_t));
    if (!entry)
        return -1;

    entry->data = (uint8_t *)kalloc(CACHE_BLOCK_SIZE);
    if (!entry->data) {
        kfree(entry);
        return -1;
    }

    entry->prim_id = prim_id;
    entry->scnd_id = scnd_id;
    entry->offset  = offset;
    entry->dirty   = 0;
    cache_memcpy(entry->data, data, CACHE_BLOCK_SIZE);

    /* Insert at MRU position (head) */
    list_add(&entry->node, &lru_list);
    num_entries++;
    return 0;
}

int cache_mark_dirty(int prim_id, int scnd_id, uint32_t offset)
{
    cache_entry_t *entry = find_entry(prim_id, scnd_id, offset);
    if (!entry)
        return -1;

    entry->dirty = 1;
    list_move(&entry->node, &lru_list);
    return 0;
}

int cache_flush(void)
{
    int written = 0;
    cache_entry_t *e;
    list_for_each_entry(e, &lru_list, node) {
        if (e->dirty && writeback_entry(e) == 0)
            written++;
    }
    return written;
}

void cache_invalidate(int prim_id, int scnd_id, uint32_t offset)
{
    cache_entry_t *entry = find_entry(prim_id, scnd_id, offset);
    if (!entry)
        return;

    if (entry->dirty)
        writeback_entry(entry);

    list_del(&entry->node);
    kfree(entry->data);
    kfree(entry);
    num_entries--;
}

void cache_stats(uint32_t *hits, uint32_t *misses, uint32_t *entries)
{
    if (hits)    *hits    = stat_hits;
    if (misses)  *misses  = stat_misses;
    if (entries) *entries = num_entries;
}